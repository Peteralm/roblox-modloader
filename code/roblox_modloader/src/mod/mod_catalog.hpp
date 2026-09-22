#pragma once

#include "RobloxModLoader/config/config_types.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace rml
{
	struct ModDefinition
	{
		std::string folder_id;
		std::filesystem::path root;
		std::optional<std::filesystem::path> native_entry;
		std::vector<std::filesystem::path> dotnet_entries;
		std::string name;
		std::int32_t priority;
		bool enabled;
		bool auto_load;
	};

	struct ModCatalogError
	{
		std::filesystem::path source;
		std::string message;
	};

	struct ModCatalogResult
	{
		std::vector<ModDefinition> mods;
		std::vector<ModCatalogError> errors;
	};

	[[nodiscard]] ModCatalogResult discover_mods(const std::filesystem::path& loader_root);
} // namespace rml
