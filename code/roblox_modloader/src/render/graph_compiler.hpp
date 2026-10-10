#pragma once

#include "RobloxModLoader/render/injection_point.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace rml::render::detail
{
	struct PassDescriptor
	{
		std::string name;
		InjectionPoint point;
		std::uint8_t priority;
		bool scene;
		std::vector<std::string> dependencies;
	};

	enum class Issue : std::uint8_t
	{
		PointUnresolved,
		NoSceneTarget,
		MissingDependency,
		CrossPointDependency,
		SceneDependency,
		DependencyCycle
	};

	struct Diagnostic
	{
		std::size_t pass;
		Issue issue;
		std::string detail;
	};

	struct PointPlan
	{
		InjectionPoint point;
		std::vector<std::size_t> offscreen;
		std::vector<std::size_t> scene;
	};

	struct Plan
	{
		std::vector<PointPlan> points;
		std::vector<Diagnostic> diagnostics;

		[[nodiscard]] const PointPlan* find(InjectionPoint point) const;
		[[nodiscard]] bool contains(InjectionPoint point) const;
		[[nodiscard]] bool has_offscreen_in_pass() const;
	};

	using Resolved = std::function<bool(InjectionPoint)>;

	[[nodiscard]] Plan compile(const std::vector<PassDescriptor>& passes, const Resolved& resolved);
	[[nodiscard]] std::string_view issue_text(Issue issue);
}
