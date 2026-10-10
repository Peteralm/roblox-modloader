#pragma once

#include "RobloxModLoader/render/commands.hpp"
#include "RobloxModLoader/render/frame_resources.hpp"
#include "RobloxModLoader/render/render_pass.hpp"
#include "RobloxModLoader/rml_export.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace RBX::Graphics
{
	class Device;
	class ShaderProgram;
}

namespace rml::render
{
	struct ShaderText
	{
		std::string metal;
		std::string hlsl;
		std::string entry_point = "main";
		std::uint32_t buffer_mask = 0;
		std::uint32_t texture_mask = 0;
	};

	class RML_EXPORT FullscreenPass : public IRenderPass
	{
	public:
		void on_device_lost() override;

	protected:
		FullscreenPass(ShaderText fragment, std::string program_name);
		FullscreenPass(ShaderText vertex, ShaderText fragment, std::string program_name);

		[[nodiscard]] bool ensure_program(const FrameContext& frame);
		[[nodiscard]] RBX::Graphics::ShaderProgram* program() const;
		void draw_fullscreen(const RenderContext& ctx);
		void draw_fullscreen(const RenderContext& ctx, RenderTarget& target, LoadOp load = LoadOp::DontCare, const ClearValue& clear = {});
		virtual bool resolve_programs(const FrameContext& frame);

	private:
		ShaderText m_vertex;
		ShaderText m_fragment;
		std::string m_program_name;
		std::shared_ptr<RBX::Graphics::ShaderProgram> m_program;
		RBX::Graphics::Device* m_device{};
		bool m_failed{};
	};

	[[nodiscard]] RML_EXPORT const ShaderText& fullscreen_vertex_shader();
}
