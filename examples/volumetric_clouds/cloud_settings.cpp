#include "cloud_settings.hpp"

#include <algorithm>

namespace clouds
{
	static float unit(const float value)
	{
		return std::clamp(value, 0.f, 1.f);
	}

	void CloudSettings::clamp()
	{
		coverage = unit(coverage);
		density = unit(density);
		cloud_type = unit(cloud_type);
		shape_factor = unit(shape_factor);
		shape_scale = std::clamp(shape_scale, 0.1f, 20.f);
		erosion_factor = unit(erosion_factor);
		erosion_scale = std::clamp(erosion_scale, 1.f, 400.f);
		base_altitude = std::clamp(base_altitude, -10000.f, 200000.f);
		thickness = std::clamp(thickness, 100.f, 50000.f);
		wind_speed = std::clamp(wind_speed, 0.f, 1000.f);
		global_wind_scale = std::clamp(global_wind_scale, 0.f, 50.f);
		evolution = std::clamp(evolution, 0.f, 10.f);
		color = RBX::Color3(unit(color.r), unit(color.g), unit(color.b));
		sun_intensity = std::clamp(sun_intensity, 0.f, 4.f);
		ambient_intensity = std::clamp(ambient_intensity, 0.f, 4.f);
		powder = unit(powder);
		multi_scattering = unit(multi_scattering);
		shadow_strength = unit(shadow_strength);
		horizon_fade = std::clamp(horizon_fade, 1000.f, 1000000.f);
		quality = std::clamp(quality, 1, 4);
	}

	bool CloudSettings::apply_preset(const std::string_view name)
	{
		if (name == "Sparse")
		{
			coverage = 0.35f;
			density = 0.4f;
			cloud_type = 0.6f;
			shape_factor = 0.95f;
			erosion_factor = 0.8f;
			base_altitude = 4000.f;
			thickness = 2500.f;
			color = RBX::Color3(1.f, 1.f, 1.f);
		}
		else if (name == "Cloudy")
		{
			coverage = 0.45f;
			density = 0.4f;
			cloud_type = 0.75f;
			shape_factor = 0.9f;
			erosion_factor = 0.8f;
			base_altitude = 3000.f;
			thickness = 4000.f;
			color = RBX::Color3(1.f, 1.f, 1.f);
		}
		else if (name == "Overcast")
		{
			coverage = 0.9f;
			density = 0.3f;
			cloud_type = 0.35f;
			shape_factor = 0.5f;
			erosion_factor = 0.5f;
			base_altitude = 3500.f;
			thickness = 5000.f;
			color = RBX::Color3(0.9f, 0.9f, 0.92f);
		}
		else if (name == "Stormy")
		{
			coverage = 0.8f;
			density = 0.55f;
			cloud_type = 1.f;
			shape_factor = 0.85f;
			erosion_factor = 0.75f;
			base_altitude = 2500.f;
			thickness = 12000.f;
			color = RBX::Color3(0.7f, 0.72f, 0.75f);
		}
		else
		{
			return false;
		}
		clamp();
		return true;
	}
}
