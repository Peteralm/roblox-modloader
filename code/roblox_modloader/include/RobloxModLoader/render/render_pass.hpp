#pragma once

#include "RobloxModLoader/render/engine_stage.hpp"
#include "RobloxModLoader/render/injection_point.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace RBX::Graphics
{
	class Device;
	class RenderCamera;
	struct GlobalShaderData;
}

namespace rml::render
{
	class Commands;
	class FrameResources;

	enum class TargetMode : std::uint8_t
	{
		Offscreen,
		Scene
	};

	enum class Pipeline : std::uint8_t
	{
		Classic,
		MainView
	};

	struct FrameContext
	{
		FrameResources& resources;
		RBX::Graphics::Device* device{};
		const RBX::Graphics::GlobalShaderData* globals{};
		const RBX::Graphics::RenderCamera* camera{};
		std::uint32_t view_width{};
		std::uint32_t view_height{};
		std::uint32_t render_width{};
		std::uint32_t render_height{};
		std::uint32_t output_width{};
		std::uint32_t output_height{};
		std::uint32_t samples{1};
		Pipeline pipeline{Pipeline::Classic};
		bool capture{};
		float delta_time{};
		std::uint64_t frame_index{};
	};

	struct RenderContext
	{
		const FrameContext& frame;
		Commands& commands;
		InjectionPoint injection_point;
		std::uint32_t occurrence{};
	};

	class IRenderPass
	{
	public:
		virtual ~IRenderPass() = default;

		IRenderPass(const IRenderPass&) = delete;
		IRenderPass& operator=(const IRenderPass&) = delete;

		[[nodiscard]] virtual std::string get_name() const = 0;
		[[nodiscard]] virtual InjectionPoint get_injection_point() const = 0;

		[[nodiscard]] virtual std::uint8_t get_priority() const
		{
			return 100;
		}

		[[nodiscard]] virtual TargetMode target_mode() const
		{
			return TargetMode::Offscreen;
		}

		[[nodiscard]] virtual std::vector<std::string_view> dependencies() const
		{
			return {};
		}

		[[nodiscard]] virtual EngineStages replaces() const
		{
			return {};
		}

		virtual void pre_render(const FrameContext&)
		{
		}

		virtual void render(const RenderContext& ctx) = 0;

		virtual void post_render(const FrameContext&)
		{
		}

		virtual void on_resize(std::uint32_t, std::uint32_t)
		{
		}

		virtual void on_device_lost()
		{
		}

		[[nodiscard]] virtual bool is_enabled() const
		{
			return m_enabled;
		}

		virtual void set_enabled(const bool enabled)
		{
			m_enabled = enabled;
		}

	protected:
		IRenderPass() = default;

	private:
		bool m_enabled = true;
	};
}
