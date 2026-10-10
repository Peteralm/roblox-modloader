#include "render/engine_stages.hpp"

#include "RobloxModLoader/internal/common.hpp"
#include "render/injection_table.hpp"

RML_LOG_SCOPE("Render");

namespace rml::render::detail
{
	EngineStages& EngineStages::instance()
	{
		static EngineStages stages;
		return stages;
	}

	SkipTokens& EngineStages::tokens()
	{
		return m_tokens;
	}

	void EngineStages::mark_hooked(const EngineStage stage)
	{
		m_hooked.fetch_or(rml::Flags<EngineStage>(stage).bits(), std::memory_order_acq_rel);
		RML_INFO("engine stage {} can be skipped", k_engine_stage_names[static_cast<std::size_t>(stage)]);
	}

	void EngineStages::mark_queues_hooked()
	{
		m_queues_hooked.store(true, std::memory_order_release);
	}

	bool EngineStages::hooked(const EngineStage stage) const
	{
		return (m_hooked.load(std::memory_order_acquire) & rml::Flags<EngineStage>(stage).bits()) != 0;
	}

	bool EngineStages::queues_hooked() const
	{
		return m_queues_hooked.load(std::memory_order_acquire);
	}

	void EngineStages::set_frame(const SkipSet& skip)
	{
		m_frame = skip;
	}

	void EngineStages::force_skip(const EngineStage stage)
	{
		m_frame.stages |= stage;
	}

	bool EngineStages::skips(const EngineStage stage) const
	{
		return m_frame.skips(stage);
	}

	bool EngineStages::skips(const QueueGroup group) const
	{
		return m_frame.skips(group);
	}
}

namespace rml::render
{
	SkipToken::SkipToken(const std::uint64_t id) noexcept :
	    m_id(id)
	{
	}

	SkipToken::~SkipToken()
	{
		reset();
	}

	SkipToken::SkipToken(SkipToken&& other) noexcept :
	    m_id(std::exchange(other.m_id, 0))
	{
	}

	SkipToken& SkipToken::operator=(SkipToken&& other) noexcept
	{
		if (this != &other)
		{
			reset();
			m_id = std::exchange(other.m_id, 0);
		}
		return *this;
	}

	void SkipToken::reset() noexcept
	{
		if (m_id == 0)
			return;
		detail::EngineStages::instance().tokens().release(m_id);
		m_id = 0;
	}

	SkipToken skip(const EngineStage stage)
	{
		auto& stages = detail::EngineStages::instance();
		if (!stages.hooked(stage))
			RML_WARN("engine stage {} cannot be skipped on this build", k_engine_stage_names[static_cast<std::size_t>(stage)]);
		return SkipToken(stages.tokens().acquire(stage));
	}

	SkipToken skip(const QueueGroup group)
	{
		if (!is_skippable(group))
			RML_WARN("queue group {} cannot be skipped on this build", k_queue_group_names[static_cast<std::size_t>(group)]);
		return SkipToken(detail::EngineStages::instance().tokens().acquire(group));
	}

	bool is_skippable(const EngineStage stage)
	{
		return detail::EngineStages::instance().hooked(stage);
	}

	bool is_skippable(const QueueGroup group)
	{
		return detail::EngineStages::instance().queues_hooked() && detail::InjectionTable::instance().has_group(group);
	}
}
