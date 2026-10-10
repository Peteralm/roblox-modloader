#pragma once

#include "RobloxModLoader/render/render_pass.hpp"

namespace rml::render
{
	class ScenePass : public IRenderPass
	{
	public:
		[[nodiscard]] TargetMode target_mode() const final
		{
			return TargetMode::Scene;
		}

	protected:
		ScenePass() = default;
	};
}
