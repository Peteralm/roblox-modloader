#include "mod_manager.hpp"

#include "RobloxModLoader/internal/common.hpp"
#include "RobloxModLoader/platform/core/early_phase.hpp"
#include "dotnet/dotnet_mod_loader.hpp"
#include "filesystem/directory.hpp"
#include "mod_catalog.hpp"
#include "native/native_mod_loader.hpp"

#include <algorithm>
#include <cctype>
#include <iterator>
#include <ranges>

RML_LOG_SCOPE("ModManager");

namespace rml
{
	std::expected<void, ModManagerError> ModManager::initialize(events::EventManager& event_manager)
	{
		const auto mods_path = get_mods_dir();

		if (!mods_path.has_value())
		{
			return std::unexpected(ModManagerError(ModManagerError::Type::DirectoryNotFound, mods_path.error()));
		}

		const auto runtime_path = filesystem::directory::get_runtime_directory();

		register_loader(std::make_unique<native::NativeModLoader>(event_manager), ModKind::Native);
		register_loader(std::make_unique<dotnet::DotnetModLoader>(runtime_path,
		                     mods_path.value() / mod_kind_folder_name(ModKind::Dotnet)),
		    ModKind::Dotnet);

		const auto catalog = discover_mods(mods_path->parent_path());
		for (const auto& error : catalog.errors)
			RML_ERROR("Skipping invalid mod metadata '{}': {}", error.source.string(), error.message);
		load_catalog(catalog);

		return {};
	}

	void ModManager::load_catalog(const ModCatalogResult& catalog) const
	{
		const auto loader_for = [this](const ModKind kind) -> IModLoader* {
			const auto found = m_loaders.find(kind);
			return found == m_loaders.end() ? nullptr : found->second.get();
		};
		IModLoader* const native = loader_for(ModKind::Native);
		IModLoader* const dotnet = loader_for(ModKind::Dotnet);

		auto errors = load_catalog(catalog, native, dotnet, config::ModLoadPhase::Normal);

		// A global-init mod is already inside the process; this pass only starts its
		// ordinary lifecycle. The loader runs on its own thread and gets here while
		// Studio is still starting, so the phase is waited for instead of sampled:
		// sampling makes adoption a coin flip between the two threads.
		constexpr unsigned bootstrap_timeout_ms = 30000;
		const auto global_init_mod = [](const ModDefinition& mod) {
			return mod.enabled && mod.auto_load && mod.load_phase == config::ModLoadPhase::GlobalInit;
		};
		if (std::ranges::any_of(catalog.mods, global_init_mod))
		{
			if (platform::wait_for_global_init_phase(bootstrap_timeout_ms))
			{
				auto adopted = load_catalog(catalog, native, dotnet, config::ModLoadPhase::GlobalInit);
				errors.insert(errors.end(), std::make_move_iterator(adopted.begin()), std::make_move_iterator(adopted.end()));
			}
			else
			{
				const std::string reason = platform::global_init_phase_diagnostic();
				for (const auto& mod : catalog.mods | std::views::filter(global_init_mod))
					errors.push_back("Global-init bootstrap did not complete for " + mod.root.string() + ": " + reason);
			}
		}

		for (const auto& error : errors)
			RML_ERROR("{}", error);
	}

	void ModManager::shutdown()
	{
		for (const auto& loader : m_loaders | std::views::values)
			loader->unload_all();
	}

	ModManager::~ModManager()
	{
		shutdown();
	}

	void ModManager::register_loader(std::unique_ptr<IModLoader> loader, const ModKind kind)
	{
		m_loaders[kind] = std::move(loader);
	}

	std::expected<void, ModManagerError> ModManager::load_directory(const std::filesystem::path& directory) const
	{
		if (!std::filesystem::exists(directory))
		{
			return std::unexpected(
			    ModManagerError(ModManagerError::Type::DirectoryNotFound, "Directory does not exist: " + directory.string()));
		}

		if (!std::filesystem::is_directory(directory))
		{
			return std::unexpected(
			    ModManagerError(ModManagerError::Type::PathNotDirectory, "Path is not a directory: " + directory.string()));
		}

		const auto loader = find_loader_for_path(directory);

		if (!loader.has_value())
		{
			return std::unexpected(
			    ModManagerError(ModManagerError::Type::NoLoaderFound, "No loader found for directory: " + directory.string()));
		}

		std::vector<std::string> errors;
		for (const auto& entry : std::filesystem::directory_iterator(directory))
		{
			if (!entry.is_regular_file())
			{
				continue;
			}

			const auto& extensions = (*loader)->extensions();
			if (std::ranges::find_if(extensions,
			        [&entry](const auto& ext) {
				        return entry.path().extension() == ext;
			        })
			    == extensions.end())
			{
				continue;
			}

			if (const auto result = (*loader)->load(entry.path()); !result.has_value())
			{
				errors.push_back("Failed to load " + entry.path().string() + ": " + result.error());
			}
		}

		if (!errors.empty())
		{
			std::string error_message = "Errors occurred while loading mods from directory:\n";
			for (const auto& error : errors)
			{
				error_message += " - " + error + "\n";
			}
			return std::unexpected(ModManagerError(ModManagerError::Type::LoadFailed, std::move(error_message)));
		}

		return {};
	}

	std::expected<void, std::string> ModManager::load(const std::filesystem::path& path) const
	{
		const auto loader = find_loader_for_path(path);
		if (!loader.has_value())
		{
			return std::unexpected("No loader found for file: " + path.string());
		}

		return (*loader)->load(path);
	}

	std::expected<void, std::string> ModManager::unload(const std::filesystem::path& path) const
	{
		const auto loader = find_loader_for_path(path);
		if (!loader.has_value())
		{
			return std::unexpected("No loader found for file: " + path.string());
		}

		return (*loader)->unload(path);
	}

	std::expected<void, std::string> ModManager::reload(const std::filesystem::path& path) const
	{
		const auto loader = find_loader_for_path(path);
		if (!loader.has_value())
		{
			return std::unexpected("No loader found for file: " + path.string());
		}

		return (*loader)->reload(path);
	}

	std::expected<std::filesystem::path, std::string> ModManager::get_mods_dir() noexcept
	{
		auto mod_loader_dir = filesystem::directory::get_mod_loader_directory() / "mods";
		if (!std::filesystem::exists(mod_loader_dir))
		{
			if (std::error_code ec; !std::filesystem::create_directories(mod_loader_dir, ec))
			{
				return std::unexpected("Failed to create mods directory: " + ec.message());
			}
		}

		return std::filesystem::path(mod_loader_dir);
	}

	std::optional<ModKind> ModManager::kind_for_path(const std::filesystem::path& path) const noexcept
	{
		std::error_code ec;
		const bool path_is_directory = std::filesystem::is_directory(path, ec);
		const auto& containing_folder = path_is_directory ? path.filename() : path.parent_path().filename();
		return mod_kind_from_folder(containing_folder.string());
	}

	std::optional<IModLoader*> ModManager::find_loader_for_path(const std::filesystem::path& path) const noexcept
	{
		const auto kind = kind_for_path(path);
		if (!kind.has_value())
		{
			return std::nullopt;
		}

		const auto it = m_loaders.find(*kind);
		if (it == m_loaders.end())
		{
			return std::nullopt;
		}

		return it->second.get();
	}
}
