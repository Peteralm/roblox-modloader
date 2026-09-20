#pragma once
#include "mod_catalog.hpp"

#include "RobloxModLoader/internal/common.hpp"
#include "RobloxModLoader/mod/events.hpp"
#include "RobloxModLoader/mod/mod_base.hpp"
#include "imod_loader.hpp"
#include "mod_kind.hpp"

namespace RBX
{
	class DataModel;
	enum class DataModelType : std::int32_t;
}

namespace rml
{
	struct ModManagerError
	{
		enum class Type
		{
			DirectoryNotFound,
			PathNotDirectory,
			NoLoaderFound,
			LoadFailed,
			UnloadFailed,
			ReloadFailed
		};

		Type type;
		std::string message;

		ModManagerError(const Type t, std::string msg) :
		    type(t),
		    message(std::move(msg))
		{
		}
	};

	class ModManager
	{
	public:
		ModManager() = default;
		~ModManager();

		ModManager(const ModManager&) = delete;
		ModManager& operator=(const ModManager&) = delete;

		[[nodiscard]] std::expected<void, ModManagerError> initialize(events::EventManager& event_manager);
		void shutdown();

		void register_loader(std::unique_ptr<IModLoader> loader, ModKind kind);

		[[nodiscard]] std::expected<void, ModManagerError> load_directory(const std::filesystem::path& directory) const;
		void load_catalog(const ModCatalogResult& catalog) const;
		[[nodiscard]] static std::vector<std::string> load_catalog(
		    const ModCatalogResult& catalog, IModLoader* native_loader, IModLoader* dotnet_loader);
		[[nodiscard]] std::expected<void, std::string> load(const std::filesystem::path& path) const;
		[[nodiscard]] std::expected<void, std::string> unload(const std::filesystem::path& path) const;
		[[nodiscard]] std::expected<void, std::string> reload(const std::filesystem::path& path) const;

		[[nodiscard]] static std::expected<std::filesystem::path, std::string> get_mods_dir() noexcept;

	private:
		[[nodiscard]] std::optional<ModKind> kind_for_path(const std::filesystem::path& path) const noexcept;
		[[nodiscard]] std::optional<IModLoader*> find_loader_for_path(const std::filesystem::path& path) const noexcept;

		std::unordered_map<ModKind, std::unique_ptr<IModLoader>> m_loaders;
	};

	inline std::vector<std::string> ModManager::load_catalog(
	    const ModCatalogResult& catalog, IModLoader* native_loader, IModLoader* dotnet_loader)
	{
		std::vector<std::string> errors;
		const auto load_entry = [&errors](IModLoader* loader, const std::filesystem::path& path) {
			if (!loader)
			{
				errors.push_back("No loader registered for " + path.string());
				return;
			}
			if (const auto loaded = loader->load(path); !loaded)
				errors.push_back("Failed to load " + path.string() + ": " + loaded.error());
		};

		for (const auto& mod : catalog.mods)
		{
			if (!mod.enabled || !mod.auto_load || mod.load_phase != config::ModLoadPhase::Normal)
				continue;
			if (mod.native_entry)
				load_entry(native_loader, *mod.native_entry);
			for (const auto& entry : mod.dotnet_entries)
				load_entry(dotnet_loader, entry);
		}
		return errors;
	}
}
