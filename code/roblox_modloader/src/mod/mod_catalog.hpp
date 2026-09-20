#pragma once

#include "RobloxModLoader/config/config_types.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace rml
{
	struct ModRootDefinition
	{
		std::string folder_id;
		std::filesystem::path root;
		std::string name;
		std::int32_t priority;
		bool enabled;
		bool auto_load;
	};

	struct NativeModDefinition
	{
		std::string folder_id;
		std::filesystem::path root;
		std::filesystem::path dll;
		std::string name;
		std::int32_t priority;
		bool enabled;
		bool auto_load;
		config::ModLoadPhase load_phase;
	};

	struct ModCatalogError
	{
		std::filesystem::path source;
		std::string message;
	};

	struct ModCatalogResult
	{
		std::vector<ModRootDefinition> roots;
		std::vector<NativeModDefinition> native_mods;
		std::vector<ModCatalogError> errors;
	};

	[[nodiscard]] ModCatalogResult discover_native_mods(const std::filesystem::path& loader_root);
} // namespace rml
