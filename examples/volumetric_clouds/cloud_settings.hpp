#pragma once

#include <RobloxModLoader/roblox/util/G3DCore.h>

#include <string_view>

namespace clouds
{
	struct CloudSettings
	{
		bool enabled{true};
		float coverage{0.45f};
		float density{0.4f};
		float cloud_type{0.75f};
		float shape_factor{0.9f};
		float shape_scale{5.f};
		float erosion_factor{0.8f};
		float erosion_scale{107.f};
		float base_altitude{3000.f};
		float thickness{4000.f};
		bool use_global_wind{true};
		float global_wind_scale{4.f};
		RBX::Vector3 wind_direction{1.f, 0.f, 0.3f};
		float wind_speed{30.f};
		float evolution{1.f};
		RBX::Vector3 global_wind{0.f, 0.f, 0.f};
		RBX::Color3 color{1.f, 1.f, 1.f};
		float sun_intensity{1.f};
		float ambient_intensity{1.f};
		float powder{0.25f};
		float multi_scattering{0.5f};
		float shadow_strength{0.6f};
		float horizon_fade{60000.f};
		int quality{3};
		int seed{0};

		void clamp();
		bool apply_preset(std::string_view name);
	};
}
