#include "render/commands_impl.hpp"

#include "RobloxModLoader/roblox/graphics/device_context.hpp"
#include "render/frame_resources_impl.hpp"

#include <algorithm>
#include <stdexcept>

namespace rml::render::detail
{
	CommandsImpl::CommandsImpl(RBX::Graphics::DeviceContext& context, RBX::Graphics::Geometry& fullscreen, const Mode mode) :
	    m_context(context),
	    m_fullscreen(fullscreen),
	    m_mode(mode)
	{
	}

	CommandsImpl::~CommandsImpl()
	{
		finish();
	}

	void CommandsImpl::begin(RenderTarget& target, const LoadOp load, const ClearValue& clear)
	{
		if (m_mode == Mode::Scene)
			throw std::logic_error("scene passes draw into the engine pass and cannot begin a target");

		auto* owned = dynamic_cast<RenderTargetImpl*>(&target);
		if (!owned || !owned->framebuffer())
			throw std::invalid_argument("the target was not created by FrameResources");

		if (m_open)
			end();

		const auto& desc = owned->desc();
		unsigned attachments = 0;
		for (std::uint8_t i = 0; i < desc.color_count; ++i)
			attachments |= 1u << i;
		if (desc.depth)
			attachments |= RBX::Graphics::PassClear::Depth;

		RBX::Graphics::PassClear pass_clear{};
		const RBX::Graphics::PassClear* clear_ptr = nullptr;
		unsigned load_mask = 0;
		if (load == LoadOp::Load)
		{
			load_mask = attachments;
		}
		else if (load == LoadOp::Clear)
		{
			pass_clear.mask = attachments;
			for (std::uint8_t i = 0; i < desc.color_count; ++i)
				std::ranges::copy(clear.color, pass_clear.color[i]);
			pass_clear.depth = clear.depth;
			clear_ptr = &pass_clear;
		}

		m_context.begin_pass(owned->framebuffer(), load_mask, attachments, clear_ptr, nullptr, 0);
		m_open = true;
	}

	void CommandsImpl::end()
	{
		if (!m_open)
			return;
		m_context.end_pass();
		m_open = false;
	}

	void CommandsImpl::finish()
	{
		end();
	}

	void CommandsImpl::set_state(const RBX::Graphics::RasterizerState& rasterizer, const RBX::Graphics::BlendState& blend, const RBX::Graphics::DepthState& depth)
	{
		m_context.set_render_state(rasterizer, blend, depth);
	}

	void CommandsImpl::bind_program(RBX::Graphics::ShaderProgram& program)
	{
		m_context.bind_program(&program);
	}

	void CommandsImpl::bind_texture(const unsigned slot, RBX::Graphics::Texture* texture, const RBX::Graphics::SamplerState& sampler)
	{
		m_context.bind_texture(slot, texture, sampler);
	}

	void CommandsImpl::bind_constants(const unsigned slot, const void* data, const std::uint32_t size)
	{
		m_context.bind_buffer_data(slot, data, size);
	}

	void CommandsImpl::draw(RBX::Graphics::Geometry& geometry, const RBX::Graphics::Geometry::Primitive primitive, const unsigned offset, const unsigned count, const unsigned instances)
	{
		if (m_mode == Mode::Offscreen && !m_open)
			throw std::logic_error("offscreen passes must begin() a target before drawing");
		m_context.draw(&geometry, primitive, offset, 0, count, instances, 0);
	}

	void CommandsImpl::draw_fullscreen()
	{
		draw(m_fullscreen, RBX::Graphics::Geometry::Primitive::Triangles, 0, 3, 1);
	}
}
