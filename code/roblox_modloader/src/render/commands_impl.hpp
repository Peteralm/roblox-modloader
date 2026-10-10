#pragma once

#include "RobloxModLoader/render/commands.hpp"

#include <cstdint>

namespace RBX::Graphics
{
	class DeviceContext;
}

namespace rml::render::detail
{
	class CommandsImpl final : public Commands
	{
	public:
		enum class Mode : std::uint8_t
		{
			Offscreen,
			Scene
		};

		CommandsImpl(RBX::Graphics::DeviceContext& context, RBX::Graphics::Geometry& fullscreen, Mode mode);
		~CommandsImpl() override;

		CommandsImpl(const CommandsImpl&) = delete;
		CommandsImpl& operator=(const CommandsImpl&) = delete;

		using Commands::begin;
		using Commands::bind_constants;

		void begin(RenderTarget& target, LoadOp load, const ClearValue& clear) override;
		void end() override;
		void set_state(const RBX::Graphics::RasterizerState& rasterizer, const RBX::Graphics::BlendState& blend, const RBX::Graphics::DepthState& depth) override;
		void bind_program(RBX::Graphics::ShaderProgram& program) override;
		void bind_texture(unsigned slot, RBX::Graphics::Texture* texture, const RBX::Graphics::SamplerState& sampler) override;
		void bind_constants(unsigned slot, const void* data, std::uint32_t size) override;
		void draw(RBX::Graphics::Geometry& geometry, RBX::Graphics::Geometry::Primitive primitive, unsigned offset, unsigned count, unsigned instances) override;
		void draw_fullscreen() override;
		void finish();

	private:
		RBX::Graphics::DeviceContext& m_context;
		RBX::Graphics::Geometry& m_fullscreen;
		Mode m_mode;
		bool m_open{};
	};
}
