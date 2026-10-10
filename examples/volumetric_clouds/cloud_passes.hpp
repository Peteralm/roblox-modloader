#pragma once

#include "cloud_state.hpp"

#include <RobloxModLoader/render/passes/fullscreen_pass.hpp>
#include <RobloxModLoader/render/passes/scene_pass.hpp>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace RBX::Graphics
{
	class ShaderProgram;
}

namespace clouds
{
	namespace pass_names
	{
		inline constexpr std::string_view DEPTH = "Clouds.Depth";
		inline constexpr std::string_view SHADOW = "Clouds.Shadow";
		inline constexpr std::string_view TRACE = "Clouds.Trace";
		inline constexpr std::string_view RECONSTRUCT = "Clouds.Reconstruct";
		inline constexpr std::string_view COMPOSITE = "Clouds.Composite";
		inline constexpr std::array<std::string_view, 5> ALL{DEPTH, SHADOW, TRACE, RECONSTRUCT, COMPOSITE};
	}

	class CloudPass : public rml::render::FullscreenPass
	{
	public:
		[[nodiscard]] rml::render::InjectionPoint get_injection_point() const override;
		void pre_render(const rml::render::FrameContext& frame) override;
		void on_device_lost() override;

	protected:
		CloudPass(std::shared_ptr<CloudState> state, rml::render::ShaderText fragment, std::string program_name);

		std::shared_ptr<CloudState> m_state;
	};

	class CloudDepthPass final : public CloudPass
	{
	public:
		explicit CloudDepthPass(std::shared_ptr<CloudState> state);

		[[nodiscard]] std::string get_name() const override;
		[[nodiscard]] std::uint8_t get_priority() const override;
		void pre_render(const rml::render::FrameContext& frame) override;
		void render(const rml::render::RenderContext& ctx) override;
		void on_device_lost() override;

	protected:
		bool resolve_programs(const rml::render::FrameContext& frame) override;

	private:
		std::shared_ptr<RBX::Graphics::ShaderProgram> m_msaa;
	};

	class CloudShadowPass final : public CloudPass
	{
	public:
		explicit CloudShadowPass(std::shared_ptr<CloudState> state);

		[[nodiscard]] std::string get_name() const override;
		[[nodiscard]] std::vector<std::string_view> dependencies() const override;
		void pre_render(const rml::render::FrameContext& frame) override;
		void render(const rml::render::RenderContext& ctx) override;
	};

	class CloudTracePass final : public CloudPass
	{
	public:
		explicit CloudTracePass(std::shared_ptr<CloudState> state);

		[[nodiscard]] std::string get_name() const override;
		[[nodiscard]] std::vector<std::string_view> dependencies() const override;
		void render(const rml::render::RenderContext& ctx) override;
	};

	class CloudReconstructPass final : public CloudPass
	{
	public:
		explicit CloudReconstructPass(std::shared_ptr<CloudState> state);

		[[nodiscard]] std::string get_name() const override;
		[[nodiscard]] std::vector<std::string_view> dependencies() const override;
		void render(const rml::render::RenderContext& ctx) override;
		void on_resize(std::uint32_t width, std::uint32_t height) override;
	};

	class CloudCompositePass final : public rml::render::ScenePass
	{
	public:
		explicit CloudCompositePass(std::shared_ptr<CloudState> state);

		[[nodiscard]] std::string get_name() const override;
		[[nodiscard]] rml::render::InjectionPoint get_injection_point() const override;
		[[nodiscard]] rml::render::EngineStages replaces() const override;
		void pre_render(const rml::render::FrameContext& frame) override;
		void render(const rml::render::RenderContext& ctx) override;
		void on_device_lost() override;

	private:
		bool ensure_programs(const rml::render::FrameContext& frame);

		struct Programs
		{
			std::shared_ptr<RBX::Graphics::ShaderProgram> shadow;
			std::shared_ptr<RBX::Graphics::ShaderProgram> sky;
			std::shared_ptr<RBX::Graphics::ShaderProgram> geometry;
			std::shared_ptr<RBX::Graphics::ShaderProgram> depth;
		};

		std::shared_ptr<CloudState> m_state;
		Programs m_programs;
		RBX::Graphics::Device* m_device{};
		bool m_failed{};
	};
}
