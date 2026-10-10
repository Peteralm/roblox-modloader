#include "render/render_graph_impl.hpp"

#include "RobloxModLoader/internal/common.hpp"

RML_LOG_SCOPE("RenderGraph");

namespace rml::render::detail
{
	bool& RenderGraphImpl::in_frame()
	{
		thread_local bool value = false;
		return value;
	}

	template<typename Fn>
	void RenderGraphImpl::guarded(Entry& entry, const std::string_view hook, Fn&& fn)
	{
		try
		{
			std::forward<Fn>(fn)();
			return;
		}
		catch (const std::exception& e)
		{
			RML_ERROR("pass '{}' threw from {}: {}", entry.name, hook, e.what());
		}
		catch (...)
		{
			RML_ERROR("pass '{}' threw an unknown exception from {}", entry.name, hook);
		}

		if (++entry.failures >= k_max_failures)
		{
			entry.faulted = true;
			RML_ERROR("pass '{}' disabled after {} failures", entry.name, entry.failures);
		}
	}

	void RenderGraphImpl::add_pass(std::unique_ptr<IRenderPass> pass)
	{
		if (!pass)
		{
			RML_WARN("rejected a null pass");
			return;
		}
		auto name = pass->get_name();
		std::lock_guard state(m_state_mutex);
		m_pending.push_back({std::move(pass), std::move(name), false});
	}

	void RenderGraphImpl::remove_pass(const std::string_view name)
	{
		if (in_frame())
		{
			std::lock_guard state(m_state_mutex);
			m_pending.push_back({nullptr, std::string(name), true});
			return;
		}

		std::lock_guard frame(m_frame_mutex);
		std::vector<std::unique_ptr<IRenderPass>> doomed;
		{
			std::lock_guard state(m_state_mutex);
			for (auto it = m_pending.begin(); it != m_pending.end();)
			{
				if (!it->remove && it->name == name)
				{
					doomed.push_back(std::move(it->pass));
					it = m_pending.erase(it);
				}
				else
				{
					++it;
				}
			}
			for (auto it = m_entries.begin(); it != m_entries.end();)
			{
				if (it->name == name)
				{
					doomed.push_back(std::move(it->pass));
					it = m_entries.erase(it);
				}
				else
				{
					++it;
				}
			}
		}
		if (!doomed.empty())
			RML_INFO("removed pass '{}'", name);
	}

	IRenderPass* RenderGraphImpl::find_pass(const std::string_view name) const
	{
		std::lock_guard state(m_state_mutex);
		const auto it = std::ranges::find(m_entries, name, &Entry::name);
		return it == m_entries.end() ? nullptr : it->pass.get();
	}

	std::vector<IRenderPass*> RenderGraphImpl::passes() const
	{
		std::lock_guard state(m_state_mutex);
		std::vector<IRenderPass*> result;
		result.reserve(m_entries.size());
		for (const auto& entry : m_entries)
		{
			if (entry.pass)
				result.push_back(entry.pass.get());
		}
		return result;
	}

	void RenderGraphImpl::apply_pending(std::vector<std::unique_ptr<IRenderPass>>& doomed)
	{
		std::lock_guard state(m_state_mutex);
		std::erase_if(m_entries, [](const Entry& entry) { return !entry.pass; });
		auto pending = std::exchange(m_pending, {});
		for (auto& op : pending)
		{
			if (op.remove)
			{
				for (auto it = m_entries.begin(); it != m_entries.end();)
				{
					if (it->name == op.name)
					{
						doomed.push_back(std::move(it->pass));
						it = m_entries.erase(it);
					}
					else
					{
						++it;
					}
				}
				continue;
			}

			if (std::ranges::any_of(m_entries, [&](const Entry& entry) { return entry.name == op.name; }))
			{
				RML_ERROR("pass '{}' already exists; the new one was rejected", op.name);
				doomed.push_back(std::move(op.pass));
				continue;
			}

			std::string point;
			try
			{
				point = to_string(op.pass->get_injection_point());
			}
			catch (const std::exception& e)
			{
				point = std::format("an unknown point ({})", e.what());
			}
			catch (...)
			{
				point = "an unknown point";
			}
			RML_INFO("added pass '{}' at {}", op.name, point);
			m_entries.push_back({std::move(op.pass), std::move(op.name)});
		}
	}

	void RenderGraphImpl::report_once(const std::string& name, const Issue issue, const std::string& detail)
	{
		if (!m_reported.emplace(name, issue).second)
			return;
		if (detail.empty())
			RML_WARN("pass '{}' {}", name, issue_text(issue));
		else
			RML_WARN("pass '{}' {} ({})", name, issue_text(issue), detail);
	}

	std::unique_lock<std::mutex> RenderGraphImpl::begin_frame(const FrameContext& frame, const Resolved& resolved, const SkipSet& tokens)
	{
		std::unique_lock frame_lock(m_frame_mutex);
		in_frame() = true;

		{
			std::vector<std::unique_ptr<IRenderPass>> doomed;
			apply_pending(doomed);
		}

		const bool resized = frame.render_width != m_width || frame.render_height != m_height;
		m_width = frame.render_width;
		m_height = frame.render_height;

		for (auto& entry : m_entries)
		{
			if (!entry.faulted && resized)
				guarded(entry, "on_resize", [&] { entry.pass->on_resize(frame.render_width, frame.render_height); });
			if (!entry.faulted)
				guarded(entry, "pre_render", [&] { entry.pass->pre_render(frame); });
		}

		std::vector<PassDescriptor> descriptors;
		std::vector<Flags<EngineStage>> replaced;
		m_plan_entries.clear();
		for (std::size_t i = 0; i < m_entries.size(); ++i)
		{
			auto& entry = m_entries[i];
			if (entry.faulted)
				continue;

			std::optional<PassDescriptor> descriptor;
			EngineStages stages;
			guarded(entry, "describe", [&] {
				if (!entry.pass->is_enabled())
					return;
				PassDescriptor built{entry.name, entry.pass->get_injection_point(), entry.pass->get_priority(), entry.pass->target_mode() == TargetMode::Scene, {}};
				for (const auto dependency : entry.pass->dependencies())
					built.dependencies.emplace_back(dependency);
				stages = entry.pass->replaces();
				descriptor = std::move(built);
			});
			if (!descriptor)
				continue;

			descriptors.push_back(std::move(*descriptor));
			replaced.push_back(stages);
			m_plan_entries.push_back(i);
		}

		m_plan = compile(descriptors, resolved);
		for (const auto& diagnostic : m_plan.diagnostics)
			report_once(descriptors[diagnostic.pass].name, diagnostic.issue, diagnostic.detail);

		std::vector<Flags<EngineStage>> planned_replaced;
		for (const auto& point : m_plan.points)
		{
			for (const auto index : point.offscreen)
				planned_replaced.push_back(replaced[index]);
			for (const auto index : point.scene)
				planned_replaced.push_back(replaced[index]);
		}
		m_skip = frame_skip_set(tokens, planned_replaced, m_plan);
		return frame_lock;
	}

	void RenderGraphImpl::render(const std::size_t plan_index, const RenderContext& ctx)
	{
		auto& entry = m_entries[m_plan_entries[plan_index]];
		if (!entry.faulted)
			guarded(entry, "render", [&] { entry.pass->render(ctx); });
	}

	void RenderGraphImpl::end_frame(const FrameContext& frame, std::unique_lock<std::mutex> frame_lock)
	{
		for (const auto& point : m_plan.points)
		{
			for (const auto* part : {&point.offscreen, &point.scene})
			{
				for (const auto index : *part)
				{
					auto& entry = m_entries[m_plan_entries[index]];
					if (!entry.faulted)
						guarded(entry, "post_render", [&] { entry.pass->post_render(frame); });
				}
			}
		}
		in_frame() = false;
	}

	void RenderGraphImpl::device_lost()
	{
		std::unique_lock<std::mutex> frame;
		if (!in_frame())
			frame = std::unique_lock(m_frame_mutex);
		for (auto& entry : m_entries)
		{
			if (entry.pass)
				guarded(entry, "on_device_lost", [&] { entry.pass->on_device_lost(); });
		}
	}

	void RenderGraphImpl::module_unloading(const std::uintptr_t begin, const std::uintptr_t end)
	{
		const auto inside = [begin, end](const IRenderPass* pass) {
			const auto vtable = *reinterpret_cast<const std::uintptr_t*>(pass);
			return vtable >= begin && vtable < end;
		};

		if (in_frame())
		{
			std::vector<std::unique_ptr<IRenderPass>> pending;
			{
				std::lock_guard state(m_state_mutex);
				for (auto& op : m_pending)
				{
					if (op.pass && inside(op.pass.get()))
						pending.push_back(std::move(op.pass));
				}
				std::erase_if(m_pending, [](const Pending& op) { return !op.remove && !op.pass; });
				for (auto& entry : m_entries)
				{
					if (entry.pass && inside(entry.pass.get()))
					{
						RML_ERROR("pass '{}' belongs to a module unloading mid-frame; it is leaked", entry.name);
						entry.faulted = true;
						m_leaked.push_back(entry.pass.release());
					}
				}
			}
			return;
		}

		std::lock_guard frame(m_frame_mutex);
		std::vector<std::unique_ptr<IRenderPass>> doomed;
		{
			std::lock_guard state(m_state_mutex);
			for (auto& op : m_pending)
			{
				if (op.pass && inside(op.pass.get()))
					doomed.push_back(std::move(op.pass));
			}
			std::erase_if(m_pending, [](const Pending& op) { return !op.remove && !op.pass; });
			for (auto& entry : m_entries)
			{
				if (entry.pass && inside(entry.pass.get()))
				{
					RML_WARN("pass '{}' was still registered when its module unloaded; removed", entry.name);
					doomed.push_back(std::move(entry.pass));
				}
			}
			std::erase_if(m_entries, [](const Entry& entry) { return !entry.pass; });
		}
	}

	const Plan& RenderGraphImpl::plan() const
	{
		return m_plan;
	}

	const SkipSet& RenderGraphImpl::skip_set() const
	{
		return m_skip;
	}

	const std::string& RenderGraphImpl::pass_name(const std::size_t plan_index) const
	{
		return m_entries[m_plan_entries[plan_index]].name;
	}
}
