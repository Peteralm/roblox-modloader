#include "RobloxModLoader/roblox/graphics/shader_source.hpp"

#include "RobloxModLoader/internal/common.hpp"
#include "RobloxModLoader/platform/graphics/shader_compiler.hpp"
#include "RobloxModLoader/roblox/graphics/device.hpp"

#include <cstring>
#include <format>

RML_LOG_SCOPE("Graphics");

namespace rml::graphics
{
	std::vector<char> make_shader_blob(const std::string_view payload, const std::uint64_t resource_masks, const std::uint32_t reserved)
	{
		std::vector<char> blob(sizeof(resource_masks) + sizeof(reserved) + payload.size());
		std::memcpy(blob.data(), &resource_masks, sizeof(resource_masks));
		std::memcpy(blob.data() + sizeof(resource_masks), &reserved, sizeof(reserved));
		std::memcpy(blob.data() + sizeof(resource_masks) + sizeof(reserved), payload.data(), payload.size());
		return blob;
	}

	static std::expected<std::shared_ptr<RBX::Graphics::Shader>, std::string> create_shader(RBX::Graphics::Device& device, const ShaderSource& source, const RBX::Graphics::Shader::Type stage, const std::string_view language, const std::string& name)
	{
		const auto compiled = platform::compile_shader(source, stage, language);
		if (!compiled)
			return std::unexpected(std::format("{}: {}", name, compiled.error()));

		const auto masks = static_cast<std::uint64_t>(compiled->texture_mask) << 32 | compiled->buffer_mask;
		auto shader = device.create_shader(stage, make_shader_blob({compiled->payload.data(), compiled->payload.size()}, masks), name);
		if (!shader)
			return std::unexpected(std::format("{}: the device rejected the shader", name));
		return shader;
	}

	std::expected<std::shared_ptr<RBX::Graphics::ShaderProgram>, std::string> create_program(RBX::Graphics::Device& device, const ShaderSource& vertex, const ShaderSource& fragment, const std::string& name)
	{
		using namespace RBX::Graphics;
		try
		{
			const auto language = device.get_shading_language();

			auto vs = create_shader(device, vertex, Shader::Type::Vertex, language, name + ".vs");
			if (!vs)
				return std::unexpected(vs.error());
			auto fs = create_shader(device, fragment, Shader::Type::Fragment, language, name + ".fs");
			if (!fs)
				return std::unexpected(fs.error());

			const std::shared_ptr<Shader> shaders[2] = {std::move(*vs), std::move(*fs)};
			auto program = device.create_shader_program(shaders, 2, name);
			if (!program)
				return std::unexpected(std::format("{}: the device rejected the program", name));
			return program;
		}
		catch (const std::exception& e)
		{
			return std::unexpected(std::format("{}: {}", name, e.what()));
		}
	}
}
