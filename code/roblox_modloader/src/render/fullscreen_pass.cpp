#include "RobloxModLoader/render/passes/fullscreen_pass.hpp"

#include "RobloxModLoader/internal/common.hpp"
#include "RobloxModLoader/roblox/graphics/device.hpp"
#include "RobloxModLoader/roblox/graphics/shader_source.hpp"

RML_LOG_SCOPE("Render");

namespace rml::render
{
	static graphics::ShaderSource view_of(const ShaderText& text)
	{
		return {text.metal, text.hlsl, text.entry_point, text.buffer_mask, text.texture_mask};
	}

	const ShaderText& fullscreen_vertex_shader()
	{
		static const ShaderText shader{
		    R"(#include <metal_stdlib>
using namespace metal;

vertex float4 rml_fullscreen_vs(uint id [[vertex_id]])
{
    float2 uv = float2((id << 1) & 2, id & 2);
    return float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
}
)",
		    R"(float4 rml_fullscreen_vs(uint id : SV_VertexID) : SV_Position
{
    float2 uv = float2((id << 1) & 2, id & 2);
    return float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
}
)",
		    "rml_fullscreen_vs",
		    0,
		    0};
		return shader;
	}

	FullscreenPass::FullscreenPass(ShaderText fragment, std::string program_name) :
	    FullscreenPass(fullscreen_vertex_shader(), std::move(fragment), std::move(program_name))
	{
	}

	FullscreenPass::FullscreenPass(ShaderText vertex, ShaderText fragment, std::string program_name) :
	    m_vertex(std::move(vertex)),
	    m_fragment(std::move(fragment)),
	    m_program_name(std::move(program_name))
	{
	}

	void FullscreenPass::on_device_lost()
	{
		m_program.reset();
		m_device = nullptr;
		m_failed = false;
	}

	bool FullscreenPass::ensure_program(const FrameContext& frame)
	{
		if (!frame.device)
			return false;
		if (m_device != frame.device)
		{
			m_program.reset();
			m_device = frame.device;
			m_failed = false;
		}
		if (m_program)
			return true;
		if (m_failed)
			return false;

		auto program = graphics::create_program(*frame.device, view_of(m_vertex), view_of(m_fragment), m_program_name);
		if (!program)
		{
			m_failed = true;
			RML_ERROR("program '{}' failed: {}", m_program_name, program.error());
			return false;
		}
		if (!resolve_programs(frame))
		{
			m_failed = true;
			return false;
		}
		m_program = std::move(*program);
		return true;
	}

	bool FullscreenPass::resolve_programs(const FrameContext&)
	{
		return true;
	}

	RBX::Graphics::ShaderProgram* FullscreenPass::program() const
	{
		return m_program.get();
	}

	void FullscreenPass::draw_fullscreen(const RenderContext& ctx)
	{
		if (!m_program)
			return;
		ctx.commands.bind_program(*m_program);
		ctx.commands.draw_fullscreen();
	}

	void FullscreenPass::draw_fullscreen(const RenderContext& ctx, RenderTarget& target, const LoadOp load, const ClearValue& clear)
	{
		ctx.commands.begin(target, load, clear);
		draw_fullscreen(ctx);
		ctx.commands.end();
	}
}
