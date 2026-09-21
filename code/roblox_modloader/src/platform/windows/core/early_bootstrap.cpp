#include "early_bootstrap.hpp"

#include "RobloxModLoader/mod/global_init_mod.hpp"
#include "RobloxModLoader/platform/core/early_phase.hpp"
#include "generated/bootstrap_profile.hpp"
#include "mod/mod_catalog.hpp"
#include "native/early_mod_registry.hpp"
#include "platform/windows/hooking/bootstrap_detour.hpp"

#include <Windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <limits>
#include <new>
#include <string>
#include <string_view>

namespace rml::platform::windows
{
	namespace
	{
		struct EngineVector
		{
			void** begin;
			void** end;
			void** capacity;
		};

		struct PendingRegistration
		{
			const char* class_name;
			const char* base_class_name;
			const void* descriptor;
		};

		struct DescriptorBatch
		{
			EngineVector* registry;
			void** storage;
			void** old_storage;
			PendingRegistration* pending;
			std::size_t old_size;
			std::size_t old_capacity;
			std::size_t new_capacity;
			std::uint32_t limit;
			std::uint32_t reserved;
			bool owns_storage;
		};

		using EngineAllocate = void* (*)(std::size_t);
		using EngineFree = void (*)(void*);

		std::atomic<BootstrapState> s_state{BootstrapState::Unarmed};
		BootstrapDetour s_detour;
		ResolvedGlobalInitWindow s_window{};
		char s_diagnostic[192]{};
		wchar_t s_log_path[32768]{};
		EngineVector* s_registry{};
		volatile std::uint8_t* s_registry_frozen{};
		std::uint32_t* s_class_count{};
		EngineAllocate s_engine_allocate{};
		EngineFree s_engine_free{};
		DescriptorBatch* s_active_batch{};

		void set_diagnostic(const char* message) noexcept
		{
			if (!message)
				message = "unknown global-init error";
			const auto length = std::min<std::size_t>(std::strlen(message), std::size(s_diagnostic) - 1);
			std::memcpy(s_diagnostic, message, length);
			s_diagnostic[length] = '\0';
			OutputDebugStringA("[RML global-init] ");
			OutputDebugStringA(s_diagnostic);
			OutputDebugStringA("\n");
		}

		const char* resolve_error_message(const ResolveError error) noexcept
		{
			switch (error)
			{
			case ResolveError::InvalidImage: return "invalid Studio PE image";
			case ResolveError::UnknownBuild: return "unsupported Studio build identity";
			case ResolveError::MissingSignature: return "global-init signature missing";
			case ResolveError::AmbiguousSignature: return "global-init signature is ambiguous";
			case ResolveError::InvalidShape: return "global-init signature shape mismatch";
			}
			return "unknown global-init resolver error";
		}

		bool rva_fits(const std::uint32_t rva, const std::size_t length, const std::uint32_t image_size) noexcept
		{
			return rva < image_size && length <= static_cast<std::size_t>(image_size - rva);
		}

		bool is_executable_rva(const IMAGE_NT_HEADERS64* nt, const std::uint32_t rva, const std::size_t length) noexcept
		{
			const auto* section = IMAGE_FIRST_SECTION(nt);
			for (std::uint16_t index = 0; index < nt->FileHeader.NumberOfSections; ++index)
			{
				const auto section_size = std::max(section[index].Misc.VirtualSize, section[index].SizeOfRawData);
				if ((section[index].Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0 && rva >= section[index].VirtualAddress
				    && rva - section[index].VirtualAddress <= section_size
				    && length <= section_size - (rva - section[index].VirtualAddress))
					return true;
			}
			return false;
		}

		std::string_view descriptor_name(const void* descriptor) noexcept
		{
			if (!descriptor)
				return {};
			// Descriptor stores `const Name& name` at +8; the Name object begins with its std::string.
			const auto* value = *reinterpret_cast<const std::byte* const*>(static_cast<const std::byte*>(descriptor) + 8);
			if (!value)
				return {};
			const auto size = *reinterpret_cast<const std::size_t*>(value + 16);
			const auto capacity = *reinterpret_cast<const std::size_t*>(value + 24);
			if (size > capacity || size > 4096)
				return {};
			const char* text = capacity < 16 ? reinterpret_cast<const char*>(value) : *reinterpret_cast<const char* const*>(value);
			return text ? std::string_view{text, size} : std::string_view{};
		}

		bool registry_shape_valid(const EngineVector* registry) noexcept
		{
			if (!registry)
				return false;
			if (!registry->begin && !registry->end && !registry->capacity)
				return true;
			return registry->begin && registry->end && registry->capacity && registry->begin <= registry->end
			    && registry->end <= registry->capacity;
		}

		void* find_class(const char* name) noexcept
		{
			if (!name || !registry_shape_valid(s_registry))
				return nullptr;
			const std::string_view wanted{name};
			for (auto** cursor = s_registry->begin; cursor != s_registry->end; ++cursor)
				if (descriptor_name(*cursor) == wanted)
					return *cursor;
			return nullptr;
		}

		bool registry_is_mutable() noexcept
		{
			return s_registry && s_registry_frozen && s_class_count && *s_registry_frozen == 0 && registry_shape_valid(s_registry);
		}

		void release_batch(DescriptorBatch* batch, const bool keep_storage) noexcept
		{
			if (!batch)
				return;
			if (batch->owns_storage && !keep_storage && batch->storage && s_engine_free)
				s_engine_free(batch->storage);
			delete[] batch->pending;
			delete batch;
		}

		void* begin_batch(const std::uint32_t class_count) noexcept
		{
			if (!class_count || s_active_batch || !registry_is_mutable() || !s_engine_allocate || !s_engine_free)
				return nullptr;
			if (class_count > std::numeric_limits<std::uint32_t>::max() - *s_class_count)
				return nullptr;

			auto* batch = new (std::nothrow) DescriptorBatch{};
			if (!batch)
				return nullptr;
			batch->pending = new (std::nothrow) PendingRegistration[class_count]{};
			if (!batch->pending)
			{
				delete batch;
				return nullptr;
			}

			batch->registry = s_registry;
			batch->old_storage = s_registry->begin;
			batch->old_size = s_registry->begin ? static_cast<std::size_t>(s_registry->end - s_registry->begin) : 0;
			batch->old_capacity = s_registry->begin ? static_cast<std::size_t>(s_registry->capacity - s_registry->begin) : 0;
			batch->new_capacity = batch->old_capacity;
			batch->limit = class_count;
			if (class_count > std::numeric_limits<std::size_t>::max() - batch->old_size)
			{
				release_batch(batch, false);
				return nullptr;
			}
			const auto required = batch->old_size + class_count;
			if (required > batch->old_capacity)
			{
				const auto grown = batch->old_capacity > (std::numeric_limits<std::size_t>::max() / 2) ?
				    required :
				    std::max(required, std::max<std::size_t>(8, batch->old_capacity * 2));
				if (grown > std::numeric_limits<std::size_t>::max() / sizeof(void*))
				{
					release_batch(batch, false);
					return nullptr;
				}
				batch->storage = static_cast<void**>(s_engine_allocate(grown * sizeof(void*)));
				if (!batch->storage)
				{
					release_batch(batch, false);
					return nullptr;
				}
				batch->new_capacity = grown;
				batch->owns_storage = true;
				if (batch->old_size)
					std::memcpy(batch->storage, batch->old_storage, batch->old_size * sizeof(void*));
			}
			else
			{
				batch->storage = batch->old_storage;
			}
			s_active_batch = batch;
			return batch;
		}

		int reserve_class(void* handle, const RmlClassRegistrationV1* registration) noexcept
		{
			auto* batch = static_cast<DescriptorBatch*>(handle);
			if (!batch || batch != s_active_batch || !rml_valid_class_registration(registration)
			    || batch->reserved >= batch->limit || !registration->class_name || !registration->base_class_name
			    || !registration->descriptor || !registration->factory || (registration->member_count && !registration->members)
			    || registration->flags != 0)
				return 1;

			const std::string_view class_name{registration->class_name};
			const std::string_view base_name{registration->base_class_name};
			if (class_name.empty() || base_name.empty() || descriptor_name(registration->descriptor) != class_name
			    || find_class(registration->class_name))
				return 2;

			bool base_found = find_class(registration->base_class_name) != nullptr;
			for (std::uint32_t index = 0; index < batch->reserved; ++index)
			{
				if (class_name == batch->pending[index].class_name)
					return 3;
				base_found = base_found || base_name == batch->pending[index].class_name;
			}
			if (!base_found)
				return 4;

			batch->pending[batch->reserved] = {registration->class_name, registration->base_class_name, registration->descriptor};
			batch->storage[batch->old_size + batch->reserved] = const_cast<void*>(registration->descriptor);
			++batch->reserved;
			return 0;
		}

		void abort_batch(void* handle) noexcept
		{
			auto* batch = static_cast<DescriptorBatch*>(handle);
			if (!batch || batch != s_active_batch)
				return;
			s_active_batch = nullptr;
			release_batch(batch, false);
		}

		void commit_batch(void* handle) noexcept
		{
			auto* batch = static_cast<DescriptorBatch*>(handle);
			if (!batch || batch != s_active_batch)
				return;
			if (!registry_is_mutable())
			{
				s_active_batch = nullptr;
				release_batch(batch, false);
				set_diagnostic("descriptor registry froze before batch commit");
				return;
			}
			s_active_batch = nullptr;
			if (batch->owns_storage)
			{
				batch->registry->begin = batch->storage;
				batch->registry->end = batch->storage + batch->old_size + batch->reserved;
				batch->registry->capacity = batch->storage + batch->new_capacity;
				if (batch->old_storage)
					s_engine_free(batch->old_storage);
			}
			else
			{
				batch->registry->end += batch->reserved;
			}
			*s_class_count += batch->reserved;
			release_batch(batch, true);
		}

		constexpr RmlDescriptorRegistrationApi kDescriptorApi{RML_DESCRIPTOR_REGISTRATION_API_VERSION, sizeof(RmlDescriptorRegistrationApi), find_class, registry_is_mutable, begin_batch, reserve_class, abort_batch, commit_batch};

		void initialize_log_path() noexcept
		{
			wchar_t executable[32768]{};
			const DWORD length = GetModuleFileNameW(nullptr, executable, static_cast<DWORD>(std::size(executable)));
			if (!length || length >= std::size(executable))
				return;
			wchar_t* separator = executable + length;
			while (separator != executable && separator[-1] != L'\\' && separator[-1] != L'/')
				--separator;
			*separator = L'\0';
			constexpr wchar_t suffix[] = L"RobloxModLoader\\logs";
			if ((separator - executable) + std::size(suffix) + 18 >= std::size(executable))
				return;
			std::memcpy(separator, suffix, sizeof(suffix));
			CreateDirectoryW(executable, nullptr);
			std::wcscat(executable, L"\\global-init.log");
			std::wcsncpy(s_log_path, executable, std::size(s_log_path) - 1);
		}

		void early_log(const int level, const char* message) noexcept
		{
			if (!message)
				return;
			OutputDebugStringA("[RML global-init] ");
			OutputDebugStringA(message);
			OutputDebugStringA("\n");
			if (!s_log_path[0])
				return;
			const HANDLE file = CreateFileW(s_log_path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (file == INVALID_HANDLE_VALUE)
				return;
			const char prefixes[][8] = {"TRACE ", "INFO  ", "WARN  ", "ERROR ", "FATAL "};
			const auto index = std::clamp(level, 0, 4);
			DWORD written{};
			WriteFile(file, prefixes[index], static_cast<DWORD>(std::strlen(prefixes[index])), &written, nullptr);
			WriteFile(file, message, static_cast<DWORD>(std::strlen(message)), &written, nullptr);
			constexpr char newline[] = "\r\n";
			WriteFile(file, newline, 2, &written, nullptr);
			CloseHandle(file);
		}

		void configure_engine_backend(const ResolvedGlobalInitWindow& window) noexcept
		{
			auto* base = window.host;
			s_registry = reinterpret_cast<EngineVector*>(base + window.profile->registry_vector_rva);
			s_registry_frozen = reinterpret_cast<volatile std::uint8_t*>(base + window.profile->registry_frozen_rva);
			s_class_count = reinterpret_cast<std::uint32_t*>(base + window.profile->class_count_rva);
			s_engine_allocate = reinterpret_cast<EngineAllocate>(base + window.profile->engine_allocate_rva);
			s_engine_free = reinterpret_cast<EngineFree>(base + window.profile->engine_free_rva);
		}

		void run_early_mods() noexcept
		{
			try
			{
				initialize_log_path();
				wchar_t executable[32768]{};
				const DWORD length = GetModuleFileNameW(nullptr, executable, static_cast<DWORD>(std::size(executable)));
				if (!length || length >= std::size(executable))
				{
					early_log(3, "Cannot resolve Studio executable path");
					return;
				}
				const auto loader_root = std::filesystem::path(executable).parent_path() / "RobloxModLoader";
				const auto catalog = discover_mods(loader_root);
				for (const auto& error : catalog.errors)
				{
					const auto text = "Skipping invalid early mod metadata '" + error.source.string() + "': " + error.message;
					early_log(3, text.c_str());
				}
				const RmlGlobalInitContext context{RML_GLOBAL_INIT_ABI_VERSION, nullptr, s_window.profile->build, early_log, &kDescriptorApi};
				native::EarlyModRegistry::instance().attach_all(catalog.mods, context);
			}
			catch (...)
			{
				early_log(4, "Unhandled exception during global-init discovery");
			}
		}

		extern "C" void global_init_bootstrap_thunk() noexcept
		{
			EarlyBootstrap::invoke();
		}
	}

	std::expected<ResolvedGlobalInitWindow, ResolveError> resolve_global_init_window(void* host_module, const std::span<const EmbeddedBootstrapProfile> profiles) noexcept
	{
		if (!host_module)
			return std::unexpected(ResolveError::InvalidImage);
		auto* host = static_cast<std::byte*>(host_module);
		const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(host);
		if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0)
			return std::unexpected(ResolveError::InvalidImage);
		const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(host + dos->e_lfanew);
		if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC
		    || !nt->OptionalHeader.SizeOfImage)
			return std::unexpected(ResolveError::InvalidImage);

		const auto profile = std::ranges::find_if(profiles, [nt](const auto& candidate) {
			return candidate.pe_timestamp == nt->FileHeader.TimeDateStamp
			    && candidate.size_of_image == nt->OptionalHeader.SizeOfImage;
		});
		if (profile == profiles.end())
			return std::unexpected(ResolveError::UnknownBuild);
		if (!profile->signature || profile->overwrite_size != BootstrapDetour::kPatchSize
		    || profile->signature_size < profile->overwrite_size
		    || !rva_fits(profile->window_rva, profile->signature_size, profile->size_of_image)
		    || !rva_fits(profile->registry_vector_rva, sizeof(EngineVector), profile->size_of_image)
		    || !rva_fits(profile->registry_frozen_rva, 1, profile->size_of_image)
		    || !rva_fits(profile->class_count_rva, sizeof(std::uint32_t), profile->size_of_image)
		    || !is_executable_rva(nt, profile->window_rva, profile->signature_size) || !is_executable_rva(nt, profile->engine_allocate_rva, 1)
		    || !is_executable_rva(nt, profile->engine_free_rva, 1))
			return std::unexpected(ResolveError::InvalidShape);

		constexpr std::array<std::uint8_t, BootstrapDetour::kPatchSize> expected_prologue{0x48, 0x89, 0x5C, 0x24, 0x20, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56};
		if (!std::equal(expected_prologue.begin(), expected_prologue.end(), profile->signature))
			return std::unexpected(ResolveError::InvalidShape);

		std::size_t matches{};
		const auto* section = IMAGE_FIRST_SECTION(nt);
		for (std::uint16_t index = 0; index < nt->FileHeader.NumberOfSections; ++index)
		{
			if ((section[index].Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0)
				continue;
			const auto section_rva = static_cast<std::uint32_t>(section[index].VirtualAddress);
			if (section_rva >= profile->size_of_image)
				continue;
			const auto declared_size = std::max(static_cast<std::uint32_t>(section[index].Misc.VirtualSize),
			    static_cast<std::uint32_t>(section[index].SizeOfRawData));
			const auto section_size = std::min(declared_size, profile->size_of_image - section_rva);
			if (section_size < profile->signature_size)
				continue;
			for (std::size_t offset = 0; offset <= section_size - profile->signature_size; ++offset)
				if (std::memcmp(host + section[index].VirtualAddress + offset, profile->signature, profile->signature_size) == 0)
					++matches;
		}
		if (!matches)
			return std::unexpected(ResolveError::MissingSignature);
		if (matches != 1)
			return std::unexpected(ResolveError::AmbiguousSignature);
		if (std::memcmp(host + profile->window_rva, profile->signature, profile->signature_size) != 0)
			return std::unexpected(ResolveError::InvalidShape);
		return ResolvedGlobalInitWindow{host, host + profile->window_rva, &*profile};
	}

	void EarlyBootstrap::arm(void*) noexcept
	{
		if (s_state.load(std::memory_order_acquire) != BootstrapState::Unarmed)
			return;
		auto resolved = resolve_global_init_window(GetModuleHandleW(nullptr), generated::kBootstrapProfiles);
		if (!resolved)
		{
			set_diagnostic(resolve_error_message(resolved.error()));
			s_state.store(BootstrapState::Failed, std::memory_order_release);
			return;
		}
		s_window = *resolved;
		configure_engine_backend(s_window);
		if (!s_detour.arm(s_window.target, reinterpret_cast<void*>(&global_init_bootstrap_thunk), s_window.profile->overwrite_size))
		{
			set_diagnostic("failed to arm global-init bootstrap detour");
			s_state.store(BootstrapState::Failed, std::memory_order_release);
			return;
		}
		set_diagnostic("global-init bootstrap armed");
		s_state.store(BootstrapState::Armed, std::memory_order_release);
	}

	BootstrapState EarlyBootstrap::state() noexcept
	{
		return s_state.load(std::memory_order_acquire);
	}

	const char* EarlyBootstrap::diagnostic() noexcept
	{
		return s_diagnostic;
	}

	void EarlyBootstrap::invoke() noexcept
	{
		BootstrapState expected = BootstrapState::Armed;
		if (!s_state.compare_exchange_strong(expected, BootstrapState::Running, std::memory_order_acq_rel))
		{
			if (const auto continuation = s_detour.trampoline())
				continuation();
			return;
		}

		const bool restored = s_detour.restore();
		run_early_mods();
		set_diagnostic(restored ? "global-init bootstrap completed" : "global-init bootstrap restore failed");
		s_state.store(restored ? BootstrapState::Completed : BootstrapState::Failed, std::memory_order_release);
		const auto continuation = restored ? s_detour.original_entry() : s_detour.trampoline();
		if (continuation)
			continuation();
	}
} // namespace rml::platform::windows

namespace rml::platform
{
	bool global_init_phase_completed() noexcept
	{
		return windows::EarlyBootstrap::state() == windows::BootstrapState::Completed;
	}
} // namespace rml::platform
