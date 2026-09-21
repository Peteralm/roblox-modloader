#include <RobloxModLoader/logger/logger.hpp>
#include <RobloxModLoader/mod/mod_base.hpp>
#include <RobloxModLoader/roblox/graphics/device.hpp>
#include <RobloxModLoader/roblox/graphics/render_pass.hpp>
#include <spdlog/spdlog.h>

#include <memory>

class shader_demo final : public ModBase
{
	std::shared_ptr<spdlog::logger> m_log;
	std::shared_ptr<RBX::Graphics::Texture> m_texture;
	bool m_done{false};

public:
	shader_demo()
	{
		name = "Shader Demo";
		version = "0.1.0";
		author = "RML";
		description = "Draws through RBXG3D";
		m_log = rml::Logger::get_logger("ShaderDemo");
	}

	void on_load() override
	{
		rml::graphics::add_render_callback([this](rml::graphics::RenderPassContext& pass) {
			if (m_done)
				return;
			m_done = true;

			using namespace RBX::Graphics;
			m_log->info("shading language {} feature level {}", pass.device->get_shading_language(), pass.device->get_feature_level());
			m_texture = pass.device->create_texture_impl(Texture::Type::Type_2D, Texture::Format::RGBA8, 64, 64, 1, 1, 1, 1, Texture::Usage::ShaderRead, "rml_demo");
			m_log->info("texture {} debug name '{}'", static_cast<void*>(m_texture.get()), m_texture ? m_texture->debug_name : "");
			m_texture.reset();
		});
	}

	void on_unload() override
	{
	}
};

extern "C"
{
	RML_MOD_ABI_EXPORT ModBase* start_mod()
	{
		return new shader_demo();
	}

	RML_MOD_ABI_EXPORT void uninstall_mod(const ModBase* mod)
	{
		delete mod;
	}
}

RML_EXPORT_MOD_ABI_VERSION()
