#include <RobloxModLoader/logger/logger.hpp>
#include <RobloxModLoader/mod/mod_base.hpp>
#include <RobloxModLoader/roblox/graphics/device.hpp>
#include <RobloxModLoader/roblox/graphics/render_pass.hpp>
#include <RobloxModLoader/roblox/graphics/shader_source.hpp>
#include <spdlog/spdlog.h>

#include <memory>

static constexpr const char* k_vertex = R"(
#include <metal_stdlib>
using namespace metal;

struct Varyings
{
	float4 position [[position]];
};

vertex Varyings rml_tint_vs(uint id [[vertex_id]])
{
	const float2 corners[3] = {float2(-1.0, -1.0), float2(3.0, -1.0), float2(-1.0, 3.0)};
	Varyings out;
	out.position = float4(corners[id], 0.0, 1.0);
	return out;
}
)";

static constexpr const char* k_fragment = R"(
#include <metal_stdlib>
using namespace metal;

fragment float4 rml_tint_fs()
{
	return float4(1.0, 0.0, 0.0, 0.25);
}
)";

class shader_demo final : public ModBase
{
	std::shared_ptr<spdlog::logger> m_log;
	std::shared_ptr<RBX::Graphics::ShaderProgram> m_program;
	std::shared_ptr<RBX::Graphics::VertexLayout> m_layout;
	std::shared_ptr<RBX::Graphics::Geometry> m_geometry;
	bool m_failed{false};

	bool prepare(RBX::Graphics::Device& device)
	{
		using namespace RBX::Graphics;
		if (m_geometry)
			return true;
		if (m_failed)
			return false;

		try
		{
			m_program = rml::graphics::create_program(device, k_vertex, k_fragment, "rml_tint");
			m_layout = device.create_vertex_layout_impl({}, {}, "rml_tint");
			m_geometry = device.create_geometry_impl(m_layout, nullptr, 0, nullptr, 0, "rml_tint");
			m_log->info("program {} layout {} geometry {}", static_cast<void*>(m_program.get()), static_cast<void*>(m_layout.get()), static_cast<void*>(m_geometry.get()));
		}
		catch (const std::exception& e)
		{
			m_log->error("shader setup failed: {}", e.what());
		}

		m_failed = !m_program || !m_layout || !m_geometry;
		return !m_failed;
	}

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
			using namespace RBX::Graphics;
			if (!pass.target || !prepare(*pass.device))
				return;

			RasterizerState rasterizer{};
			rasterizer.cull_mode = RasterizerState::Cull_None;
			BlendState blend{};
			blend.color_mask = BlendState::Color_All;
			blend.src_rgb = BlendState::Factor_SrcAlpha;
			blend.dst_rgb = BlendState::Factor_InvSrcAlpha;
			blend.src_alpha = BlendState::Factor_One;
			blend.dst_alpha = BlendState::Factor_Zero;
			DepthState depth{};
			depth.function = DepthState::Function_Always;

			pass.context->begin_pass(pass.target, PassClear::All, PassClear::All, nullptr, nullptr, 0);
			pass.context->set_render_state(rasterizer, blend, depth);
			pass.context->bind_program(m_program.get());
			pass.context->draw(m_geometry.get(), Geometry::Primitive::Triangles, 0, 0, 3, 1, 0);
			pass.context->end_pass();
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
