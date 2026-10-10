#include "cloud_passes.hpp"

#include "shaders_embedded.hpp"

#include <RobloxModLoader/render/commands.hpp>
#include <RobloxModLoader/roblox/graphics/device.hpp>
#include <RobloxModLoader/roblox/graphics/shader_source.hpp>
#include <RobloxModLoader/roblox/graphics/texture.hpp>

#include <format>
#include <stdexcept>

namespace clouds
{
	using namespace RBX::Graphics;
	using rml::render::FrameContext;
	using rml::render::InjectionPoint;
	using rml::render::LoadOp;
	using rml::render::RenderContext;

	static SamplerState linear_wrap()
	{
		return SamplerState::make(SamplerState::Filter_Linear, SamplerState::Address_Wrap);
	}

	static SamplerState point_clamp()
	{
		return SamplerState::make(SamplerState::Filter_Point, SamplerState::Address_Clamp);
	}

	static SamplerState linear_clamp()
	{
		return SamplerState::make(SamplerState::Filter_Linear, SamplerState::Address_Clamp);
	}

	static void opaque_state(rml::render::Commands& commands)
	{
		commands.set_state(RasterizerState::make(RasterizerState::Cull_None), BlendState::opaque(), DepthState::make(DepthState::Function_Always, false));
	}

	static rml::render::ShaderText fragment(const std::string_view defines, const shaders::Source& pass, const std::string_view entry, const std::uint32_t texture_mask)
	{
		rml::render::ShaderText text;
		text.hlsl = std::format("{}{}\n{}", defines, shaders::common.hlsl, pass.hlsl);
		text.metal = std::format("{}#define RML_ENTRY_{} 1\n{}\n{}", defines, entry, shaders::common.metal, pass.metal);
		text.entry_point = std::string(entry);
		text.buffer_mask = k_frame_mask;
		text.texture_mask = texture_mask;
		return text;
	}

	static std::shared_ptr<ShaderProgram> build(Device& device, const rml::render::ShaderText& text, const std::string& name)
	{
		const auto& vertex = rml::render::fullscreen_vertex_shader();
		auto program = rml::graphics::create_program(device, {vertex.metal, vertex.hlsl, vertex.entry_point, vertex.buffer_mask, vertex.texture_mask}, {text.metal, text.hlsl, text.entry_point, text.buffer_mask, text.texture_mask}, name);
		if (!program)
			throw std::runtime_error(program.error());
		return std::move(*program);
	}

	CloudPass::CloudPass(std::shared_ptr<CloudState> state, rml::render::ShaderText fragment_text, std::string program_name) :
	    FullscreenPass(std::move(fragment_text), std::move(program_name)),
	    m_state(std::move(state))
	{
	}

	InjectionPoint CloudPass::get_injection_point() const
	{
		return InjectionPoint::at(rml::render::FramePoint::MainAfterOpaque);
	}

	void CloudPass::pre_render(const FrameContext&)
	{
		set_enabled(m_state->active());
	}

	void CloudPass::on_device_lost()
	{
		FullscreenPass::on_device_lost();
		m_state->device_lost();
	}

	CloudDepthPass::CloudDepthPass(std::shared_ptr<CloudState> state) :
	    CloudPass(std::move(state), fragment("", shaders::depth, "DepthPS", 0x1), "rml_clouds_depth")
	{
	}

	std::string CloudDepthPass::get_name() const
	{
		return std::string(pass_names::DEPTH);
	}

	std::uint8_t CloudDepthPass::get_priority() const
	{
		return 10;
	}

	void CloudDepthPass::pre_render(const FrameContext& frame)
	{
		m_state->begin_frame(frame);
		CloudPass::pre_render(frame);
	}

	bool CloudDepthPass::resolve_programs(const FrameContext& frame)
	{
		try
		{
			m_msaa = build(*frame.device, fragment("#define RML_MSAA 1\n", shaders::depth, "DepthPS", 0x1), "rml_clouds_depth_msaa");
			return true;
		}
		catch (const std::exception& e)
		{
			m_state->log().error("Cloud depth program failed: {}", e.what());
			return false;
		}
	}

	void CloudDepthPass::on_device_lost()
	{
		m_msaa.reset();
		CloudPass::on_device_lost();
	}

	void CloudDepthPass::render(const RenderContext& ctx)
	{
		auto* depth = ctx.frame.resources.texture(rml::render::resource_names::SCENE_DEPTH);
		m_state->update(ctx.frame, depth);
		auto* target = ctx.frame.resources.target(targets::SCENE_DISTANCE, m_state->half({Texture::Format::R32F}));
		if (!target)
			return;

		const bool readable = depth && (depth->usage & static_cast<std::uint32_t>(Texture::Usage::ShaderRead)) != 0;
		if (!readable || !ensure_program(ctx.frame))
		{
			rml::render::ClearValue clear;
			clear.color = {-1.f, -1.f, -1.f, -1.f};
			ctx.commands.begin(*target, LoadOp::Clear, clear);
			ctx.commands.end();
			return;
		}

		ctx.commands.begin(*target, LoadOp::DontCare);
		opaque_state(ctx.commands);
		ctx.commands.bind_program(depth->samples > 1 ? *m_msaa : *program());
		ctx.commands.bind_texture(0, depth, point_clamp());
		ctx.commands.bind_constants(k_frame_slot, m_state->constants());
		ctx.commands.draw_fullscreen();
		ctx.commands.end();
	}

	CloudShadowPass::CloudShadowPass(std::shared_ptr<CloudState> state) :
	    CloudPass(std::move(state), fragment("", shaders::trace, "ShadowMapPS", 0x7), "rml_clouds_shadow_map")
	{
	}

	std::string CloudShadowPass::get_name() const
	{
		return std::string(pass_names::SHADOW);
	}

	std::vector<std::string_view> CloudShadowPass::dependencies() const
	{
		return {pass_names::DEPTH};
	}

	void CloudShadowPass::pre_render(const FrameContext&)
	{
		set_enabled(m_state->shadows());
	}

	void CloudShadowPass::render(const RenderContext& ctx)
	{
		auto* target = ctx.frame.resources.target(targets::SHADOW, CloudState::shadow_desc());
		if (!target || !ensure_program(ctx.frame))
			return;
		ctx.commands.begin(*target, LoadOp::DontCare);
		opaque_state(ctx.commands);
		ctx.commands.bind_program(*program());
		ctx.commands.bind_texture(0, m_state->shape(), linear_wrap());
		ctx.commands.bind_texture(1, m_state->detail(), linear_wrap());
		ctx.commands.bind_texture(2, m_state->weather(), linear_wrap());
		ctx.commands.bind_constants(k_frame_slot, m_state->constants());
		ctx.commands.draw_fullscreen();
		ctx.commands.end();
	}

	CloudTracePass::CloudTracePass(std::shared_ptr<CloudState> state) :
	    CloudPass(std::move(state), fragment("", shaders::trace, "TracePS", 0xF), "rml_clouds_trace")
	{
	}

	std::string CloudTracePass::get_name() const
	{
		return std::string(pass_names::TRACE);
	}

	std::vector<std::string_view> CloudTracePass::dependencies() const
	{
		return {pass_names::DEPTH};
	}

	void CloudTracePass::render(const RenderContext& ctx)
	{
		auto& resources = ctx.frame.resources;
		auto* target = resources.target(targets::TRACE, m_state->half({Texture::Format::RGBA16F, Texture::Format::RG32F}));
		auto* scene = resources.find(targets::SCENE_DISTANCE);
		if (!target || !scene || !ensure_program(ctx.frame))
			return;
		ctx.commands.begin(*target, LoadOp::DontCare);
		opaque_state(ctx.commands);
		ctx.commands.bind_program(*program());
		ctx.commands.bind_texture(0, m_state->shape(), linear_wrap());
		ctx.commands.bind_texture(1, m_state->detail(), linear_wrap());
		ctx.commands.bind_texture(2, m_state->weather(), linear_wrap());
		ctx.commands.bind_texture(3, scene->color(0), point_clamp());
		ctx.commands.bind_constants(k_frame_slot, m_state->constants());
		ctx.commands.draw_fullscreen();
		ctx.commands.end();
	}

	CloudReconstructPass::CloudReconstructPass(std::shared_ptr<CloudState> state) :
	    CloudPass(std::move(state), fragment("", shaders::reconstruct, "ReconstructPS", 0xF), "rml_clouds_reconstruct")
	{
	}

	std::string CloudReconstructPass::get_name() const
	{
		return std::string(pass_names::RECONSTRUCT);
	}

	std::vector<std::string_view> CloudReconstructPass::dependencies() const
	{
		return {pass_names::TRACE};
	}

	void CloudReconstructPass::on_resize(std::uint32_t, std::uint32_t)
	{
		m_state->invalidate_history();
	}

	void CloudReconstructPass::render(const RenderContext& ctx)
	{
		auto& resources = ctx.frame.resources;
		const auto desc = m_state->half({Texture::Format::RGBA16F, Texture::Format::R32F});
		auto* previous = resources.target(targets::HISTORY[m_state->history_read()], desc);
		auto* next = resources.target(targets::HISTORY[m_state->history_write()], desc);
		auto* trace = resources.find(targets::TRACE);
		auto* scene = resources.find(targets::SCENE_DISTANCE);
		if (!previous || !next || !trace || !scene || !ensure_program(ctx.frame))
			return;
		ctx.commands.begin(*next, LoadOp::DontCare);
		opaque_state(ctx.commands);
		ctx.commands.bind_program(*program());
		ctx.commands.bind_texture(0, trace->color(0), point_clamp());
		ctx.commands.bind_texture(1, trace->color(1), point_clamp());
		ctx.commands.bind_texture(2, previous->color(0), point_clamp());
		ctx.commands.bind_texture(3, scene->color(0), point_clamp());
		ctx.commands.bind_constants(k_frame_slot, m_state->constants());
		ctx.commands.draw_fullscreen();
		ctx.commands.end();
		m_state->commit_history();
	}

	CloudCompositePass::CloudCompositePass(std::shared_ptr<CloudState> state) :
	    m_state(std::move(state))
	{
	}

	std::string CloudCompositePass::get_name() const
	{
		return std::string(pass_names::COMPOSITE);
	}

	InjectionPoint CloudCompositePass::get_injection_point() const
	{
		return InjectionPoint::at(rml::render::FramePoint::MainAfterOpaque);
	}

	rml::render::EngineStages CloudCompositePass::replaces() const
	{
		return m_state->active() ? rml::render::EngineStages{rml::render::EngineStage::Clouds} : rml::render::EngineStages{};
	}

	void CloudCompositePass::pre_render(const FrameContext&)
	{
		set_enabled(m_state->active());
	}

	void CloudCompositePass::on_device_lost()
	{
		m_programs = {};
		m_device = nullptr;
		m_failed = false;
		m_state->device_lost();
	}

	bool CloudCompositePass::ensure_programs(const FrameContext& frame)
	{
		if (!frame.device)
			return false;
		if (m_device != frame.device)
		{
			m_programs = {};
			m_device = frame.device;
			m_failed = false;
		}
		if (m_programs.depth)
			return true;
		if (m_failed)
			return false;
		try
		{
			auto& device = *frame.device;
			m_programs.shadow = build(device, fragment("", shaders::composite, "ShadowPS", 0xC), "rml_clouds_shadow");
			m_programs.sky = build(device, fragment("#define RML_COMPOSITE_SKY 1\n", shaders::composite, "CompositePS", 0x1), "rml_clouds_composite_sky");
			m_programs.geometry = build(device, fragment("#define RML_COMPOSITE_SKY 0\n", shaders::composite, "CompositePS", 0x1), "rml_clouds_composite_geometry");
			m_programs.depth = build(device, fragment("", shaders::composite, "CloudDepthPS", 0x7), "rml_clouds_depth_write");
			return true;
		}
		catch (const std::exception& e)
		{
			m_failed = true;
			m_programs = {};
			m_state->log().error("Cloud composite programs failed: {}", e.what());
			return false;
		}
	}

	void CloudCompositePass::render(const RenderContext& ctx)
	{
		auto& resources = ctx.frame.resources;
		auto* history = resources.find(targets::HISTORY[m_state->history_read()]);
		auto* scene = resources.find(targets::SCENE_DISTANCE);
		if (!history || !scene || !ensure_programs(ctx.frame))
			return;

		auto& commands = ctx.commands;
		const auto raster = RasterizerState::make(RasterizerState::Cull_None);
		const auto blend = BlendState::make(BlendState::Factor_SrcAlpha, BlendState::Factor_InvSrcAlpha, BlendState::Factor_Zero, BlendState::Factor_One);
		commands.bind_constants(k_frame_slot, m_state->constants());
		commands.bind_texture(2, scene->color(0), point_clamp());

		if (auto* shadow = m_state->shadows() ? resources.find(targets::SHADOW) : nullptr)
		{
			commands.bind_texture(3, shadow->color(0), linear_clamp());
			commands.set_state(raster, BlendState::make(BlendState::Factor_Zero, BlendState::Factor_InvSrcAlpha, BlendState::Factor_Zero, BlendState::Factor_One), DepthState::make(DepthState::Function_Less, false));
			commands.bind_program(*m_programs.shadow);
			commands.draw_fullscreen();
		}

		commands.bind_texture(0, history->color(0), point_clamp());
		commands.set_state(raster, blend, DepthState::make(DepthState::Function_GreaterEqual, false));
		commands.bind_program(*m_programs.sky);
		commands.draw_fullscreen();
		commands.set_state(raster, blend, DepthState::make(DepthState::Function_Less, false));
		commands.bind_program(*m_programs.geometry);
		commands.draw_fullscreen();

		commands.bind_texture(1, history->color(1), point_clamp());
		commands.set_state(raster, BlendState::make(BlendState::Factor_One, BlendState::Factor_Zero, BlendState::Color_None), DepthState::make(DepthState::Function_Greater, true));
		commands.bind_program(*m_programs.depth);
		commands.draw_fullscreen();
	}
}
