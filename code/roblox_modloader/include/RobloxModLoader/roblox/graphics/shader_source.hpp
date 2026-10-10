#pragma once

#include "RobloxModLoader/rml_export.hpp"

#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace RBX::Graphics
{
	class Device;
	class ShaderProgram;
}

namespace rml::graphics
{
	struct ShaderSource
	{
		std::string_view metal;
		std::string_view hlsl;
		std::string_view entry_point = "main";
		std::uint32_t buffer_mask = 0;
		std::uint32_t texture_mask = 0;
	};

	RML_EXPORT std::vector<char> make_shader_blob(std::string_view payload, std::uint64_t resource_masks = 0, std::uint32_t reserved = 0);
	RML_EXPORT std::expected<std::shared_ptr<RBX::Graphics::ShaderProgram>, std::string> create_program(RBX::Graphics::Device& device, const ShaderSource& vertex, const ShaderSource& fragment, const std::string& name);
}
