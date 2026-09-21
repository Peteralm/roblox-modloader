#pragma once

#include "RobloxModLoader/rml_export.hpp"

#include <cstdint>
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
	RML_EXPORT std::vector<char> make_shader_blob(std::string_view source, std::uint64_t buffer_mask = 0, std::uint32_t reserved = 0);
	RML_EXPORT std::shared_ptr<RBX::Graphics::ShaderProgram> create_program(RBX::Graphics::Device& device, std::string_view vertex_source, std::string_view fragment_source, const std::string& name);
}
