#pragma once

#include "RobloxModLoader/roblox/graphics/shader.hpp"
#include "RobloxModLoader/roblox/graphics/shader_source.hpp"

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace rml::platform
{
	struct CompiledShader
	{
		std::vector<char> payload;
		std::uint32_t buffer_mask{};
		std::uint32_t texture_mask{};
	};

	[[nodiscard]] std::expected<CompiledShader, std::string> compile_shader(const graphics::ShaderSource& source, RBX::Graphics::Shader::Type stage, std::string_view shading_language);
}
