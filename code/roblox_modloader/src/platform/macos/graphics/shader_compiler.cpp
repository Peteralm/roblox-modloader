#include "RobloxModLoader/platform/graphics/shader_compiler.hpp"

namespace rml::platform
{
	std::expected<CompiledShader, std::string> compile_shader(const graphics::ShaderSource& source, RBX::Graphics::Shader::Type, std::string_view)
	{
		if (source.metal.empty())
			return std::unexpected("no Metal source for the Metal backend");
		return CompiledShader{std::vector<char>(source.metal.begin(), source.metal.end()), source.buffer_mask, source.texture_mask};
	}
}
