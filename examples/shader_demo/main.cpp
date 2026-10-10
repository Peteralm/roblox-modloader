#include <RobloxModLoader/logger/logger.hpp>
#include <RobloxModLoader/mod/mod_base.hpp>
#include <RobloxModLoader/render/render.hpp>
#include <spdlog/spdlog.h>

#include <memory>
#include <string>
#include <string_view>

static constexpr const char* k_metal_fragment = R"(
#include <metal_stdlib>
using namespace metal;

fragment float4 rml_tint_fs()
{
	return float4(1.0, 0.0, 0.0, 0.25);
}
)";

static constexpr const char* k_hlsl_fragment = R"(
float4 rml_tint_fs() : SV_Target
{
	return float4(1.0, 0.0, 0.0, 0.25);
}
)";

static constexpr std::string_view k_pass_name = "ShaderDemo.Tint";

class TintPass final : public rml::render::FullscreenPass
{
public:
	TintPass() :
	    FullscreenPass({k_metal_fragment, k_hlsl_fragment, "rml_tint_fs"}, "rml_tint")
	{
	}

	[[nodiscard]] std::string get_name() const override
	{
		return std::string(k_pass_name);
	}

	[[nodiscard]] rml::render::InjectionPoint get_injection_point() const override
	{
		return rml::render::InjectionPoint::at(rml::render::FramePoint::FrameEnd);
	}

	[[nodiscard]] rml::render::TargetMode target_mode() const override
	{
		return rml::render::TargetMode::Scene;
	}

	void render(const rml::render::RenderContext& ctx) override
	{
		using namespace RBX::Graphics;
		if (!ensure_program(ctx.frame))
			return;
		ctx.commands.set_state(RasterizerState::make(RasterizerState::Cull_None), BlendState::make(BlendState::Factor_SrcAlpha, BlendState::Factor_InvSrcAlpha, BlendState::Factor_One, BlendState::Factor_Zero), DepthState::make(DepthState::Function_Always, false));
		draw_fullscreen(ctx);
	}
};

class shader_demo final : public ModBase
{
public:
	shader_demo()
	{
		name = "Shader Demo";
		version = "0.2.0";
		author = "RML";
		description = "Draws a tint through the render graph";
	}

	void on_load() override
	{
		rml::render::graph().add_pass(std::make_unique<TintPass>());
	}

	void on_unload() override
	{
		rml::render::graph().remove_pass(k_pass_name);
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
