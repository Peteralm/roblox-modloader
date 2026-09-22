#pragma once

#include "RobloxModLoader/rml_export.hpp"
#include "RobloxModLoader/roblox/content_id.hpp"
#include "RobloxModLoader/roblox/content_provider_temporary_id_factory.hpp"

#include <expected>
#include <filesystem>
#include <string>

namespace rml::assets
{
	RML_EXPORT std::expected<RBX::ContentProviderTemporaryIdFactory*, std::string> temporary_id_factory();

	RML_EXPORT std::expected<RBX::ContentId, std::string> register_file(const std::filesystem::path& path);
	RML_EXPORT std::expected<RBX::ContentId, std::string> register_bytes(const std::filesystem::path& cache_file, const void* data, std::size_t size);
}
