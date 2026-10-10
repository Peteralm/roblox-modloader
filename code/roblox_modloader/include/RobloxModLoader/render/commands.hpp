#pragma once

#include "RobloxModLoader/roblox/graphics/buffer.hpp"
#include "RobloxModLoader/roblox/graphics/types.hpp"

#include <array>
#include <cstdint>
#include <type_traits>

namespace RBX::Graphics
{
	class ShaderProgram;
	class Texture;
}

namespace rml::render
{
	class RenderTarget;

	enum class LoadOp : std::uint8_t
	{
		Load,
		Clear,
		DontCare
	};

	struct ClearValue
	{
		std::array<float, 4> color{};
		float depth{};
	};

	class Commands
	{
	public:
		virtual ~Commands() = default;

		virtual void begin(RenderTarget& target, LoadOp load, const ClearValue& clear) = 0;
		virtual void end() = 0;
		virtual void set_state(const RBX::Graphics::RasterizerState& rasterizer, const RBX::Graphics::BlendState& blend, const RBX::Graphics::DepthState& depth) = 0;
		virtual void bind_program(RBX::Graphics::ShaderProgram& program) = 0;
		virtual void bind_texture(unsigned slot, RBX::Graphics::Texture* texture, const RBX::Graphics::SamplerState& sampler) = 0;
		virtual void bind_constants(unsigned slot, const void* data, std::uint32_t size) = 0;
		virtual void draw(RBX::Graphics::Geometry& geometry, RBX::Graphics::Geometry::Primitive primitive, unsigned offset, unsigned count, unsigned instances) = 0;
		virtual void draw_fullscreen() = 0;

		void begin(RenderTarget& target, const LoadOp load)
		{
			begin(target, load, ClearValue{});
		}

		template<typename T>
		void bind_constants(const unsigned slot, const T& value)
		{
			static_assert(std::is_trivially_copyable_v<T>);
			bind_constants(slot, &value, static_cast<std::uint32_t>(sizeof(T)));
		}
	};
}
