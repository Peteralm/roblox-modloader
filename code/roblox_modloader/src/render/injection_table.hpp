#pragma once

#include "RobloxModLoader/render/injection_point.hpp"

#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <set>
#include <unordered_map>

namespace RBX::Graphics
{
	class SceneManager;
}

namespace rml::render::detail
{
	class InjectionTable
	{
	public:
		static constexpr std::size_t k_max_engine_groups = 32;

		static InjectionTable& instance();

		void resolve(InjectionPoint point);
		void resolve_stage(EngineStage stage);
		void resolve_queues();
		[[nodiscard]] bool resolved(InjectionPoint point) const;
		void fired(InjectionPoint point, std::uint64_t frame);
		[[nodiscard]] InjectionStatus status(InjectionPoint point, std::uint64_t frame) const;

		void load_queue_names();
		[[nodiscard]] std::optional<QueueGroup> group_of(std::uint32_t engine_index) const;
		[[nodiscard]] std::optional<QueueGroup> group_of(const RBX::Graphics::SceneManager& scene, const void* group) const;
		[[nodiscard]] bool has_group(QueueGroup group) const;

	private:
		mutable std::mutex m_mutex;
		std::set<std::uint32_t> m_resolved;
		std::unordered_map<std::uint32_t, std::uint64_t> m_fired;
		std::array<std::optional<QueueGroup>, k_max_engine_groups> m_groups{};
		std::bitset<static_cast<std::size_t>(QueueGroup::Count)> m_present;
		bool m_queues{};
	};
}
