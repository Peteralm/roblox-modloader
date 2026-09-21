#pragma once

#include "RobloxModLoader/memory/module.hpp"
#include "RobloxModLoader/mod/global_init_mod.hpp"
#include "RobloxModLoader/mod/mod_base.hpp"
#include "mod/mod_catalog.hpp"

#include <expected>
#include <memory>
#include <mutex>
#include <span>
#include <unordered_map>

namespace rml::native
{
	enum class EarlyModStatus
	{
		NotFound,
		Failed,
		Attached,
		Adopted
	};

	// Startup owns attach_all; normal loading consumes each successful attachment
	// once. Failed entries are terminal and MUST NOT fall back to normal loading.
	// Calls are serialized. Startup must finish before normal loading begins;
	// descriptor callbacks must not re-enter this registry.
	class EarlyModRegistry
	{
	public:
		struct Adoption
		{
			std::unique_ptr<memory::module> module;
			std::filesystem::path root;
			ModBase::start_type start = nullptr;
			void (*uninstall)(const ModBase*) = nullptr;
		};

		static EarlyModRegistry& instance();
		void attach_all(std::span<const ModDefinition> definitions, const RmlGlobalInitContext& context);
		[[nodiscard]] std::expected<Adoption, std::string> adopt(const std::filesystem::path& path);
		[[nodiscard]] EarlyModStatus status(const std::filesystem::path& path) const;
		[[nodiscard]] bool is_pinned(const std::filesystem::path& path) const;

	private:
		struct Entry
		{
			EarlyModStatus status = EarlyModStatus::Failed;
			bool pinned = false;
			Adoption adoption;
		};
		static std::filesystem::path key(const std::filesystem::path& path);
		mutable std::mutex m_mutex;
		std::unordered_map<std::filesystem::path, Entry> m_entries;
	};
} // namespace rml::native
