#include "render/skip_set.hpp"

namespace rml::render::detail
{
	static void add(SkipSet& set, const InjectionPoint point)
	{
		if (point.kind() == InjectionKind::Stage)
			set.stages |= point.engine_stage();
		else if (point.kind() == InjectionKind::Queue)
			set.queues.set(static_cast<std::size_t>(point.queue_group()));
	}

	std::uint64_t SkipTokens::acquire(const EngineStage stage)
	{
		return acquire_point(InjectionPoint::stage(stage, Side::Instead));
	}

	std::uint64_t SkipTokens::acquire(const QueueGroup group)
	{
		return acquire_point(InjectionPoint::queue(group, Side::Instead));
	}

	std::uint64_t SkipTokens::acquire_point(const InjectionPoint target)
	{
		std::lock_guard lock(m_mutex);
		const auto id = m_next++;
		m_tokens.emplace(id, target);
		return id;
	}

	void SkipTokens::release(const std::uint64_t id)
	{
		std::lock_guard lock(m_mutex);
		m_tokens.erase(id);
	}

	SkipSet SkipTokens::snapshot() const
	{
		std::lock_guard lock(m_mutex);
		SkipSet set;
		for (const auto& [id, point] : m_tokens)
			add(set, point);
		return set;
	}

	SkipSet frame_skip_set(const SkipSet& tokens, const std::vector<Flags<EngineStage>>& replaced, const Plan& plan)
	{
		auto set = tokens;
		for (const auto stages : replaced)
			set.stages |= stages;
		for (const auto& point : plan.points)
		{
			if (point.point.side() == Side::Instead)
				add(set, point.point);
		}
		return set;
	}
}
