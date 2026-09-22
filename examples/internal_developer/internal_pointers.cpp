#include "internal_pointers.hpp"

#include "RobloxModLoader/internal/platform.hpp"
#include "RobloxModLoader/logger/logger.hpp"
#include "RobloxModLoader/memory/string_anchor.hpp"
#include "RobloxModLoader/platform/memory/host_image.hpp"
#include "RobloxModLoader/util/compile_time_helpers.hpp"

#include <cstring>
#include <expected>
#include <format>
#include <iterator>
#include <span>
#include <string>

#if defined(RML_WINDOWS)
	#include <windows.h>
#endif

RML_LOG_SCOPE("InternalDeveloper")

namespace internal_developer
{
	static EnginePointers g_engine_pointers{};

	EnginePointers& engine_pointers() noexcept
	{
		return g_engine_pointers;
	}

#if defined(RML_WINDOWS)
	namespace
	{
		// The literal Studio puts in its own title bar. It comes from the engine's source, so it
		// survives the churn that moves every instruction around it — unlike the byte signature
		// this used to carry, which pinned the caller's stack frame and died on 0.739.
		constexpr std::string_view k_internal_marker = "[Internal]";

		[[nodiscard]] bool reads_as_marker(const std::uintptr_t address)
		{
			const auto* text = reinterpret_cast<const char*>(address);
			return std::string_view(text, k_internal_marker.size()) == k_internal_marker
			    && text[k_internal_marker.size()] == '\0';
		}

		[[nodiscard]] bool is_writable(const void* address)
		{
			MEMORY_BASIC_INFORMATION info{};
			if (!VirtualQuery(address, &info, sizeof(info)) || info.State != MEM_COMMIT)
				return false;

			constexpr DWORD writable = PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
			return (info.Protect & writable) != 0;
		}

		// Studio asks the engine whether this build is internal and then picks the literal, so the
		// owner of the flags is the call right before the lea that materializes it.
		[[nodiscard]] std::expected<void*, std::string> callee_before_marker(const rml::memory::AnchoredFunction& function)
		{
			const std::span code(static_cast<const std::uint8_t*>(function.start), function.size);
			const auto base = reinterpret_cast<std::uintptr_t>(function.start);

			std::uintptr_t last_call = 0;
			void* callee = nullptr;
			std::size_t matches = 0;

			for (std::size_t i = 0; i + 7 <= code.size(); ++i)
			{
				if (code[i] == 0xE8)
				{
					std::int32_t displacement;
					std::memcpy(&displacement, code.data() + i + 1, sizeof(displacement));
					last_call = base + i + 5 + displacement;
					continue;
				}

				if ((code[i] & 0xF8) != 0x48 || code[i + 1] != 0x8D || (code[i + 2] & 0xC7) != 0x05)
					continue;

				std::int32_t displacement;
				std::memcpy(&displacement, code.data() + i + 3, sizeof(displacement));
				if (!reads_as_marker(base + i + 7 + displacement))
					continue;

				++matches;
				callee = reinterpret_cast<void*>(last_call);
			}

			if (matches != 1)
				return std::unexpected(std::format("expected one '{}' reference inside the title function, found {}",
				    k_internal_marker, matches));
			if (!callee)
				return std::unexpected("the marker is not preceded by a call in its own function");

			// isInternal() opens by testing a global byte; anything else means the chain drifted.
			const auto* entry = static_cast<const std::uint8_t*>(callee);
			if (entry[0] != 0x80 || entry[1] != 0x3D)
				return std::unexpected(std::format("the callee at 0x{:X} does not start with 'cmp byte ptr [rip + d], 0'",
				    reinterpret_cast<std::uintptr_t>(callee)));

			return callee;
		}

		// The flags are collected by shape inside the callee's own bounds. The exception table gives
		// those bounds when the function has an entry; a leaf like this one often has none, and then
		// the walk stops at the first CC padding byte instead, capped so it can never reach the next
		// function and hand a mod a pointer to write to.
		[[nodiscard]] std::span<const std::uint8_t> body_of(void* function)
		{
			if (const auto entry = rml::memory::function_containing(function))
				return {static_cast<const std::uint8_t*>(entry->start), entry->size};

			constexpr std::size_t ceiling = 0x80;
			const auto* bytes = static_cast<const std::uint8_t*>(function);
			std::size_t size = 0;
			while (size < ceiling && bytes[size] != 0xCC)
				++size;
			return {bytes, size};
		}

		[[nodiscard]] std::expected<void, std::string> collect_flags(void* is_internal)
		{
			const auto code = body_of(is_internal);
			if (code.size() < 7)
				return std::unexpected("the resolved isInternal has no body to read");
			const auto base = reinterpret_cast<std::uintptr_t>(code.data());

			bool** slots[] = {&g_engine_pointers.channel_flag, &g_engine_pointers.internal_flag};
			std::size_t found = 0;

			for (std::size_t i = 0; i + 7 <= code.size() && found < std::size(slots); ++i)
			{
				if (code[i] != 0x80 || code[i + 1] != 0x3D || code[i + 6] != 0x00)
					continue;

				std::int32_t displacement;
				std::memcpy(&displacement, code.data() + i + 2, sizeof(displacement));
				auto* flag = reinterpret_cast<bool*>(base + i + 7 + displacement);

				if (!is_writable(flag))
					return std::unexpected(std::format("the flag at 0x{:X} is not writable memory",
					    reinterpret_cast<std::uintptr_t>(flag)));

				*slots[found++] = flag;
				i += 6;
			}

			if (found == 0)
				return std::unexpected("isInternal tests no global byte; the flags could not be resolved");

			// A build that folds both flags into one tests it once; writing it twice is harmless.
			if (found == 1)
				g_engine_pointers.internal_flag = g_engine_pointers.channel_flag;

			return {};
		}
	}

	bool resolve_engine_pointers()
	{
		const auto functions = rml::memory::functions_referencing_string(k_internal_marker);
		if (functions.size() != 1)
		{
			RML_ERROR("Expected one function referencing '{}', found {}", k_internal_marker, functions.size());
			return false;
		}

		const auto is_internal = callee_before_marker(functions.front());
		if (!is_internal)
		{
			RML_ERROR("{}", is_internal.error());
			return false;
		}

		g_engine_pointers.is_internal = *is_internal;

		if (const auto flags = collect_flags(*is_internal); !flags)
		{
			RML_ERROR("{}", flags.error());
			g_engine_pointers = {};
			return false;
		}

		RML_INFO("Resolved isInternal at 0x{:X} through the '{}' literal",
		    reinterpret_cast<std::uintptr_t>(g_engine_pointers.is_internal), k_internal_marker);
		return g_engine_pointers.complete();
	}
#else
	static constexpr auto engine_batch()
	{
		// clang-format off
		return rml::memory::make_batch<
			{
			    "IS_INTERNAL",
			    "F4 03 00 AA ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? 9F 02 00 71 0A 5C 80 39 0B 30 40 A9 28 11 88 9A 5F 01 00 F1 69 B1 80 9A",
			    [](const rml::memory::handle ptr) {
				    const auto is_internal = ptr.sub(4).bl();

				    g_engine_pointers.is_internal = is_internal.as<void*>();
				    g_engine_pointers.channel_flag = is_internal.adrp().as<bool*>();
				    g_engine_pointers.internal_flag = is_internal.add(8).adrp().as<bool*>();
			    },
			}
		>();
		// clang-format on
	}

	bool resolve_engine_pointers()
	{
		const rml::memory::module studio{rml::platform::studio_image_name()};

		const auto [batch, hash] = engine_batch();

		if (!rml::memory::batch_runner::run(batch, studio))
		{
			RML_ERROR("Signature scan failed; Roblox Studio was probably updated");
			return false;
		}

		if (!g_engine_pointers.complete())
		{
			RML_ERROR("Signature matched but the engine flags were not resolved");
			return false;
		}

		return true;
	}
#endif
}
