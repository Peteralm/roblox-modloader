#pragma once

#include "RobloxModLoader/render/engine_stage.hpp"
#include "render/skip_set.hpp"

#include <atomic>
#include <cstdint>

namespace rml::render::detail
{
	class EngineStages
	{
	public:
		static EngineStages& instance();

		[[nodiscard]] SkipTokens& tokens();
		void mark_hooked(EngineStage stage);
		void mark_queues_hooked();
		[[nodiscard]] bool hooked(EngineStage stage) const;
		[[nodiscard]] bool queues_hooked() const;
		void set_frame(const SkipSet& skip);
		void force_skip(EngineStage stage);
		[[nodiscard]] bool skips(EngineStage stage) const;
		[[nodiscard]] bool skips(QueueGroup group) const;

	private:
		SkipTokens m_tokens;
		std::atomic<std::uint64_t> m_hooked{0};
		std::atomic<bool> m_queues_hooked{false};
		SkipSet m_frame;
	};
}
