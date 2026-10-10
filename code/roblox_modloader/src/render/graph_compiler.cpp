#include "render/graph_compiler.hpp"

#include "render/injection_traits.hpp"

#include <algorithm>
#include <map>
#include <tuple>
#include <unordered_map>

namespace rml::render::detail
{
	const PointPlan* Plan::find(const InjectionPoint point) const
	{
		const auto it = std::ranges::find(points, point, &PointPlan::point);
		return it == points.end() ? nullptr : &*it;
	}

	bool Plan::contains(const InjectionPoint point) const
	{
		return find(point) != nullptr;
	}

	bool Plan::has_offscreen_in_pass() const
	{
		return std::ranges::any_of(points, [](const PointPlan& plan) {
			return !plan.offscreen.empty() && traits(plan.point).state == PassState::InPass;
		});
	}

	std::string_view issue_text(const Issue issue)
	{
		switch (issue)
		{
		case Issue::PointUnresolved:
			return "is placed at an injection point this build does not resolve";
		case Issue::NoSceneTarget:
			return "is a scene pass at an injection point without a scene target";
		case Issue::MissingDependency:
			return "depends on a pass that is not planned";
		case Issue::CrossPointDependency:
			return "depends on a pass at another injection point";
		case Issue::SceneDependency:
			return "is an offscreen pass depending on a scene pass";
		case Issue::DependencyCycle:
			return "is part of a dependency cycle";
		}
		return "has an unknown issue";
	}

	static std::vector<std::size_t> order_group(const std::vector<PassDescriptor>& passes, const std::vector<std::size_t>& members, const std::unordered_map<std::string_view, std::size_t>& by_name, const std::vector<bool>& planned, std::vector<Diagnostic>& diagnostics)
	{
		std::unordered_map<std::size_t, std::size_t> local;
		for (std::size_t i = 0; i < members.size(); ++i)
			local.emplace(members[i], i);

		std::vector<std::vector<std::size_t>> edges(members.size());
		std::vector<std::size_t> in_degree(members.size(), 0);
		for (std::size_t i = 0; i < members.size(); ++i)
		{
			const auto& pass = passes[members[i]];
			for (const auto& dependency : pass.dependencies)
			{
				const auto named = by_name.find(dependency);
				if (named == by_name.end() || !planned[named->second])
				{
					diagnostics.push_back({members[i], Issue::MissingDependency, dependency});
					continue;
				}
				const auto found = local.find(named->second);
				if (found == local.end())
				{
					diagnostics.push_back({members[i], Issue::CrossPointDependency, dependency});
					continue;
				}
				if (passes[named->second].scene && !pass.scene)
				{
					diagnostics.push_back({members[i], Issue::SceneDependency, dependency});
					continue;
				}
				edges[found->second].push_back(i);
				++in_degree[i];
			}
		}

		const auto later = [&](const std::size_t a, const std::size_t b) {
			return std::tuple(passes[members[a]].priority, members[a]) > std::tuple(passes[members[b]].priority, members[b]);
		};

		std::vector<std::size_t> ready;
		for (std::size_t i = 0; i < members.size(); ++i)
		{
			if (in_degree[i] == 0)
				ready.push_back(i);
		}
		std::ranges::make_heap(ready, later);

		std::vector<std::size_t> ordered;
		ordered.reserve(members.size());
		while (!ready.empty())
		{
			std::ranges::pop_heap(ready, later);
			const auto next = ready.back();
			ready.pop_back();
			ordered.push_back(members[next]);
			for (const auto dependent : edges[next])
			{
				if (--in_degree[dependent] == 0)
				{
					ready.push_back(dependent);
					std::ranges::push_heap(ready, later);
				}
			}
		}

		if (ordered.size() == members.size())
			return ordered;

		for (std::size_t i = 0; i < members.size(); ++i)
		{
			if (in_degree[i] != 0)
				diagnostics.push_back({members[i], Issue::DependencyCycle, {}});
		}
		ordered = members;
		std::ranges::stable_sort(ordered, {}, [&](const std::size_t index) { return passes[index].priority; });
		return ordered;
	}

	Plan compile(const std::vector<PassDescriptor>& passes, const Resolved& resolved)
	{
		Plan plan;
		std::vector<bool> planned(passes.size(), false);
		std::map<std::uint32_t, std::vector<std::size_t>> groups;
		for (std::size_t i = 0; i < passes.size(); ++i)
		{
			const auto& pass = passes[i];
			if (!resolved(pass.point))
			{
				plan.diagnostics.push_back({i, Issue::PointUnresolved, to_string(pass.point)});
				continue;
			}
			if (pass.scene && traits(pass.point).scene_target == SceneTarget::None)
			{
				plan.diagnostics.push_back({i, Issue::NoSceneTarget, to_string(pass.point)});
				continue;
			}
			planned[i] = true;
			groups[pass.point.key()].push_back(i);
		}

		std::unordered_map<std::string_view, std::size_t> by_name;
		for (std::size_t i = 0; i < passes.size(); ++i)
			by_name.emplace(passes[i].name, i);

		for (const auto& [key, members] : groups)
		{
			PointPlan point{passes[members.front()].point, {}, {}};
			for (const auto index : order_group(passes, members, by_name, planned, plan.diagnostics))
				(passes[index].scene ? point.scene : point.offscreen).push_back(index);
			plan.points.push_back(std::move(point));
		}
		return plan;
	}
}
