#pragma once

#include "RobloxModLoader/render/injection_point.hpp"
#include "RobloxModLoader/util/flags.hpp"
#include "render/graph_compiler.hpp"

#include <bitset>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace rml::render::detail
{
	struct SkipSet
	{
		Flags<EngineStage> stages;
		std::bitset<static_cast<std::size_t>(QueueGroup::Count)> queues;

		[[nodiscard]] bool skips(const EngineStage stage) const
		{
			return stages.contains(stage);
		}

		[[nodiscard]] bool skips(const QueueGroup group) const
		{
			return queues.test(static_cast<std::size_t>(group));
		}
	};

	class SkipTokens
	{
	public:
		[[nodiscard]] std::uint64_t acquire(EngineStage stage);
		[[nodiscard]] std::uint64_t acquire(QueueGroup group);
		void release(std::uint64_t id);
		[[nodiscard]] SkipSet snapshot() const;

	private:
		std::uint64_t acquire_point(InjectionPoint target);

		mutable std::mutex m_mutex;
		std::uint64_t m_next{1};
		std::unordered_map<std::uint64_t, InjectionPoint> m_tokens;
	};

	[[nodiscard]] SkipSet frame_skip_set(const SkipSet& tokens, const std::vector<Flags<EngineStage>>& replaced, const Plan& plan);
}
