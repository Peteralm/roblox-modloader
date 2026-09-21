#include "early_mod_registry.hpp"

#include <new>
#include <utility>
#if defined(_WIN32)
	#include <windows.h>
#endif

namespace rml::native
{
	namespace
	{
		// Keep SEH in a leaf with no objects requiring C++ unwinding. Normal ABI
		// queries can throw; the two early exports must catch internally (noexcept).
		int invoke_cpp(int (*call)(void*), void* data) noexcept
		{
			try
			{
				return call(data);
			}
			catch (...)
			{
				return -1;
			}
		}

		int invoke_contained(int (*call)(void*), void* data) noexcept
		{
#if defined(_WIN32) && defined(_MSC_VER)
			__try
			{
				return invoke_cpp(call, data);
			}
			__except (GetExceptionCode() == 0xe06d7363 ? EXCEPTION_CONTINUE_SEARCH : EXCEPTION_EXECUTE_HANDLER)
			{
				return -2;
			}
#else
			return invoke_cpp(call, data);
#endif
		}

		struct Preflight
		{
			EarlyModRegistry::Adoption& adoption;
			const RmlGlobalInitContext& context;
			rml_global_init_type early = nullptr;
			const char* error = "Exception or SEH fault during global-init preflight";
			static int run(void* data)
			{
				auto& self = *static_cast<Preflight*>(data);
				auto& module = *self.adoption.module;
				self.adoption.start = module.get_export("start_mod").as<ModBase::start_type>();
				self.adoption.uninstall = module.get_export("uninstall_mod").as<void (*)(const ModBase*)>();
				auto normal_abi = module.get_export("rml_abi_version").as<rml_abi_version_type>();
				auto early_abi = module.get_export("rml_global_init_abi_version").as<rml_global_init_abi_version_type>();
				self.early = module.get_export("rml_global_init").as<rml_global_init_type>();
				if (!self.adoption.start || !self.adoption.uninstall || !normal_abi || !early_abi || !self.early)
				{
					self.error = "Missing mandatory normal or global-init export";
					return 1;
				}
				if (self.context.abi_version != RML_GLOBAL_INIT_ABI_VERSION || !self.context.studio_build_utf8
				    || !self.context.log || !rml_valid_descriptor_api(self.context.descriptors))
				{
					self.error = "Invalid global-init context or descriptor API";
					return 1;
				}
				if (normal_abi() != RML_ABI_VERSION || early_abi() != RML_GLOBAL_INIT_ABI_VERSION)
				{
					self.error = "Mismatched normal or global-init ABI version";
					return 1;
				}
				return 0;
			}
		};

		// Track loader-owned reservations outside the SEH frame so an early fault
		// cannot strand them. Module-owned storage is never freed by this adapter.
		struct DescriptorSession
		{
			struct Batch
			{
				Batch* next;
				void* backend;
				bool failed = false;
			};
			const RmlDescriptorRegistrationApi& backend;
			Batch* batches = nullptr;
			bool failed = false;
			static thread_local DescriptorSession* active;

			Batch* find(void* handle) noexcept
			{
				for (auto* batch = batches; batch; batch = batch->next)
					if (batch == handle)
						return batch;
				failed = true;
				return nullptr;
			}
			void remove(Batch* batch) noexcept
			{
				auto** link = &batches;
				while (*link != batch)
					link = &(*link)->next;
				*link = batch->next;
			}
			static void* find_class(const char* name) noexcept
			{
				return active && name ? active->backend.find_class(name) : nullptr;
			}
			static bool mutable_registry() noexcept
			{
				return active && active->backend.registry_is_mutable();
			}
			static void* begin(std::uint32_t count) noexcept
			{
				if (!active || !count || !mutable_registry())
					return nullptr;
				auto* batch = new (std::nothrow) Batch{active->batches, nullptr};
				if (!batch)
					return nullptr;
				active->batches = batch;
				batch->backend = active->backend.begin_batch(count);
				if (!batch->backend)
				{
					active->remove(batch);
					delete batch;
					return nullptr;
				}
				return batch;
			}
			static int reserve(void* handle, const RmlClassRegistrationV1* value) noexcept
			{
				if (!active)
					return 1;
				auto* batch = active->find(handle);
				if (!batch)
					return 1;
				// Do not inspect any tail fields until the versioned header passes.
				if (batch->failed || !rml_valid_class_registration(value) || !value->class_name || !value->base_class_name
				    || !value->descriptor || !value->factory || (value->member_count && !value->members) || value->flags != 0)
				{
					batch->failed = true;
					return 1;
				}
				const int result = active->backend.reserve_class(batch->backend, value);
				batch->failed = result != 0;
				return result;
			}
			static void abort(void* handle) noexcept
			{
				if (!active)
					return;
				auto* batch = active->find(handle);
				if (!batch)
					return;
				auto* backend_batch = batch->backend;
				active->remove(batch);
				delete batch;
				if (backend_batch)
					active->backend.abort_batch(backend_batch);
			}
			static void commit(void* handle) noexcept
			{
				if (!active)
					return;
				auto* batch = active->find(handle);
				if (!batch)
					return;
				if (batch->failed)
				{
					active->failed = true;
					abort(handle);
					return;
				}
				// Retain tracking until publication returns, allowing fault cleanup.
				active->backend.commit_batch(batch->backend);
				active->remove(batch);
				delete batch;
			}
			static int abort_one(void* data)
			{
				auto& self = *static_cast<DescriptorSession*>(data);
				abort(self.batches);
				return 0;
			}
			void finish() noexcept
			{
				if (batches)
					failed = true;
				while (batches)
					invoke_contained(abort_one, this);
			}
			static constexpr RmlDescriptorRegistrationApi api{RML_DESCRIPTOR_REGISTRATION_API_VERSION, sizeof(RmlDescriptorRegistrationApi), find_class, mutable_registry, begin, reserve, abort, commit};
		};
		thread_local DescriptorSession* DescriptorSession::active = nullptr;

		struct EarlyCall
		{
			rml_global_init_type early;
			const RmlGlobalInitContext& context;
			static int run(void* data)
			{
				auto& self = *static_cast<EarlyCall*>(data);
				return self.early(&self.context);
			}
		};
	}

	EarlyModRegistry& EarlyModRegistry::instance()
	{
		static EarlyModRegistry registry;
		return registry;
	}

	std::filesystem::path EarlyModRegistry::key(const std::filesystem::path& path)
	{
		auto result = std::filesystem::weakly_canonical(path);
#if defined(_WIN32)
		auto native = result.native();
		CharLowerBuffW(native.data(), static_cast<DWORD>(native.size()));
		result = std::move(native);
#endif
		return result;
	}

	void EarlyModRegistry::attach_all(std::span<const ModDefinition> definitions, const RmlGlobalInitContext& context)
	{
		std::scoped_lock lock(m_mutex);
		for (const auto& definition : definitions)
		{
			if (!definition.enabled || !definition.auto_load || definition.load_phase != config::ModLoadPhase::GlobalInit
			    || !definition.native_entry)
				continue;
			auto [it, inserted] = m_entries.try_emplace(key(*definition.native_entry));
			if (!inserted)
				continue;
			auto& entry = it->second;
			try
			{
				entry.adoption.module = std::make_unique<memory::module>(*definition.native_entry);
				if (auto result = entry.adoption.module->attach(); !result)
				{
					if (context.log)
						context.log(3, result.error().c_str());
					continue;
				}
				Preflight preflight{entry.adoption, context};
				if (invoke_contained(Preflight::run, &preflight) != 0)
				{
					(void)entry.adoption.module->detach();
					if (context.log)
						context.log(3, preflight.error);
					continue;
				}
				entry.adoption.root = definition.root;
				const auto root = definition.root.u8string();
				auto mod_context = context;
				mod_context.mod_root_utf8 = reinterpret_cast<const char*>(root.c_str());
				mod_context.descriptors = &DescriptorSession::api;
#if defined(_WIN32)
				HMODULE pinned = nullptr;
				if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
				        reinterpret_cast<LPCWSTR>(preflight.early),
				        &pinned))
				{
					(void)entry.adoption.module->detach();
					context.log(3, "Failed to pin global-init module");
					continue;
				}
#endif
				// Pin BEFORE entry: even a failed/faulting mod may have published pointers.
				// On non-Windows, retain the attach reference for process lifetime.
				entry.pinned = true;
				DescriptorSession session{*context.descriptors};
				auto* previous = std::exchange(DescriptorSession::active, &session);
				EarlyCall call{preflight.early, mod_context};
				const int result = invoke_contained(EarlyCall::run, &call);
				session.finish();
				DescriptorSession::active = previous;
				if (result == 0 && !session.failed)
					entry.status = EarlyModStatus::Attached;
				else
				{
					// The code is the module's own; without it every failure looks alike.
					const auto message =
					    "Global-init entry failed or faulted (code " + std::to_string(result) + (session.failed ? ", reservations abandoned" : "") + "); module remains pinned";
					context.log(3, message.c_str());
				}
			}
			catch (...)
			{
				if (entry.adoption.module && !entry.pinned)
					(void)entry.adoption.module->detach();
				if (context.log)
					context.log(3, "Exception while attaching global-init module");
			}
		}
	}

	std::expected<EarlyModRegistry::Adoption, std::string> EarlyModRegistry::adopt(const std::filesystem::path& path)
	{
		std::scoped_lock lock(m_mutex);
		const auto it = m_entries.find(key(path));
		if (it == m_entries.end() || it->second.status != EarlyModStatus::Attached)
			return std::unexpected("Global-init module is not available for adoption");
		it->second.status = EarlyModStatus::Adopted;
		return std::move(it->second.adoption);
	}

	EarlyModStatus EarlyModRegistry::status(const std::filesystem::path& path) const
	{
		std::scoped_lock lock(m_mutex);
		const auto it = m_entries.find(key(path));
		return it == m_entries.end() ? EarlyModStatus::NotFound : it->second.status;
	}

	bool EarlyModRegistry::is_pinned(const std::filesystem::path& path) const
	{
		std::scoped_lock lock(m_mutex);
		const auto it = m_entries.find(key(path));
		return it != m_entries.end() && it->second.pinned;
	}
} // namespace rml::native
