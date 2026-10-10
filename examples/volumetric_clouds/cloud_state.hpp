#pragma once

#include "cloud_settings.hpp"
#include "noise_volumes.hpp"

#include <RobloxModLoader/render/frame_resources.hpp>
#include <RobloxModLoader/render/render_pass.hpp>
#include <RobloxModLoader/roblox/util/G3DCore.h>
#include <spdlog/spdlog.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <future>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string_view>

namespace RBX::Graphics
{
	class Device;
	class Texture;
}

namespace clouds
{
	using Matrix = std::array<float, 16>;

	struct alignas(16) CloudFrame
	{
		Matrix inv_view_proj;
		Matrix prev_view_proj;
		Matrix view_proj;
		float camera_pos[4];
		float camera_delta[4];
		float sun_dir[4];
		float sun_color[4];
		float ambient_top[4];
		float ambient_bottom[4];
		float fog_color[4];
		float layer[4];
		float shape[4];
		float erosion[4];
		float wind[4];
		float weather[4];
		float albedo[4];
		float history_size[4];
		float screen_size[4];
		float params[4];
		float temporal[4];
		float depth_info[4];
		float motion[4];
		float shadow[4];
		float advect[4];
	};

	static_assert(sizeof(CloudFrame) % 16 == 0);

	inline constexpr unsigned k_frame_slot = 2;
	inline constexpr std::uint32_t k_frame_mask = 1u << k_frame_slot;
	inline constexpr std::uint32_t k_shadow_size = 256;

	namespace targets
	{
		inline constexpr std::string_view SCENE_DISTANCE = "clouds.scene_distance";
		inline constexpr std::string_view TRACE = "clouds.trace";
		inline constexpr std::string_view SHADOW = "clouds.shadow";
		inline constexpr std::array<std::string_view, 2> HISTORY{"clouds.history.0", "clouds.history.1"};
	}

	class CloudState
	{
	public:
		CloudState(std::filesystem::path cache_file, std::shared_ptr<spdlog::logger> log);

		void begin_frame(const rml::render::FrameContext& frame);
		void update(const rml::render::FrameContext& frame, const RBX::Graphics::Texture* depth);
		void commit_history();
		void invalidate_history();
		void device_lost();

		[[nodiscard]] bool active() const;
		[[nodiscard]] bool shadows() const;
		[[nodiscard]] const CloudFrame& constants() const;
		[[nodiscard]] RBX::Graphics::Texture* shape() const;
		[[nodiscard]] RBX::Graphics::Texture* detail() const;
		[[nodiscard]] RBX::Graphics::Texture* weather() const;
		[[nodiscard]] std::uint32_t history_read() const;
		[[nodiscard]] std::uint32_t history_write() const;
		[[nodiscard]] rml::render::TargetDesc half(std::initializer_list<RBX::Graphics::Texture::Format> colors) const;
		[[nodiscard]] static rml::render::TargetDesc shadow_desc();
		[[nodiscard]] spdlog::logger& log() const;

	private:
		bool ensure_noise(RBX::Graphics::Device& device);

		std::shared_ptr<spdlog::logger> m_log;
		std::future<NoiseSet> m_noise_future;
		std::optional<NoiseSet> m_noise;
		RBX::Graphics::Device* m_device{};
		std::shared_ptr<RBX::Graphics::Texture> m_shape;
		std::shared_ptr<RBX::Graphics::Texture> m_detail;
		std::shared_ptr<RBX::Graphics::Texture> m_weather;
		CloudSettings m_settings;
		bool m_active{};
		bool m_was_active{};
		bool m_logged{};
		CloudFrame m_frame{};
		std::uint32_t m_width{};
		std::uint32_t m_height{};
		std::uint32_t m_history_index{};
		bool m_history_valid{};
		std::optional<std::chrono::steady_clock::time_point> m_last_time;
		std::optional<Matrix> m_prev_view_proj;
		RBX::Vector3 m_prev_camera;
		RBX::Vector3 m_prev_view_dir;
		double m_shape_offset[2]{};
		double m_detail_offset[2]{};
		double m_shape_evolution{};
		double m_detail_evolution{};
	};
}
