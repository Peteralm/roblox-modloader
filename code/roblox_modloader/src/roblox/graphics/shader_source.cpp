#include "RobloxModLoader/roblox/graphics/shader_source.hpp"

#include "RobloxModLoader/internal/common.hpp"
#include "RobloxModLoader/roblox/graphics/device.hpp"

#include <cstring>

RML_LOG_SCOPE("Graphics");

namespace rml::graphics
{
	std::vector<char> make_shader_blob(const std::string_view source, const std::uint64_t buffer_mask, const std::uint32_t reserved)
	{
		std::vector<char> blob(sizeof(buffer_mask) + sizeof(reserved) + source.size());
		std::memcpy(blob.data(), &buffer_mask, sizeof(buffer_mask));
		std::memcpy(blob.data() + sizeof(buffer_mask), &reserved, sizeof(reserved));
		std::memcpy(blob.data() + sizeof(buffer_mask) + sizeof(reserved), source.data(), source.size());
		return blob;
	}

	std::shared_ptr<RBX::Graphics::ShaderProgram> create_program(RBX::Graphics::Device& device, const std::string_view vertex_source, const std::string_view fragment_source, const std::string& name)
	{
		using namespace RBX::Graphics;
		const std::shared_ptr<Shader> shaders[2] = {
		    device.create_shader(Shader::Type::Vertex, make_shader_blob(vertex_source), name + ".vs"),
		    device.create_shader(Shader::Type::Fragment, make_shader_blob(fragment_source), name + ".fs"),
		};
		if (!shaders[0] || !shaders[1])
			return nullptr;
		return device.create_shader_program(shaders, 2, name);
	}
}
