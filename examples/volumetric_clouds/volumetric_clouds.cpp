#include "volumetric_clouds.hpp"

#include <lua.h>
#include <lualib.h>

namespace clouds
{
	VolumetricClouds::VolumetricClouds()
	{
		CloudRegistry::instance().add(this, m_settings);
	}

	VolumetricClouds::~VolumetricClouds()
	{
		CloudRegistry::instance().remove(this);
	}

	bool VolumetricClouds::placed_in_world() const
	{
		for (const RBX::Instance* node = parent; node; node = node->parent)
		{
			const auto name = node->get_class_name().to_string();
			if (name == "Lighting" || name == "Workspace")
				return true;
		}
		return false;
	}

	void VolumetricClouds::on_ancestor_changed(const RBX::AncestorChanged& change)
	{
		call_engine_base(&RBX::Instance::on_ancestor_changed, change);
		CloudRegistry::instance().set_placed(this, placed_in_world());
	}

	int VolumetricClouds::apply_preset(lua_State* L)
	{
		const int top = lua_gettop(L);
		const char* name = top > 0 && lua_isstring(L, top) ? lua_tolstring(L, top, nullptr) : nullptr;
		if (!name || !m_settings.apply_preset(name))
			luaL_errorL(L, "ApplyPreset expects \"Sparse\", \"Cloudy\", \"Overcast\" or \"Stormy\"");
		CloudRegistry::instance().update(this, m_settings);
		return 0;
	}

	int VolumetricClouds::set_global_wind(lua_State* L)
	{
		const int top = lua_gettop(L);
		if (top < 3 || !lua_isnumber(L, top - 2) || !lua_isnumber(L, top - 1) || !lua_isnumber(L, top))
			luaL_errorL(L, "SetGlobalWind expects three numbers");
		m_settings.global_wind = RBX::Vector3(static_cast<float>(lua_tonumberx(L, top - 2, nullptr)), static_cast<float>(lua_tonumberx(L, top - 1, nullptr)), static_cast<float>(lua_tonumberx(L, top, nullptr)));
		CloudRegistry::instance().update(this, m_settings);
		return 0;
	}

	void VolumetricClouds::define(rml::InitContext& context)
	{
		using Self = VolumetricClouds;
		using S = CloudSettings;
		using rml::reflection::SliderScaling;
		context.define_class<Self>("VolumetricClouds")
		    .insert_category("Environment")
		    .explorer_order(5)
		    .preferred_parent("Lighting")
		    .icon_of("Clouds")
		    .property("Enabled", &Self::get<&S::enabled>, &Self::set<&S::enabled>).category("Behavior").order(0)
		    .property("Quality", &Self::get<&S::quality>, &Self::set<&S::quality>).category("Behavior").order(1).slider(1, 4, 3)
		    .property("Seed", &Self::get<&S::seed>, &Self::set<&S::seed>).category("Behavior").order(2)
		    .property("Coverage", &Self::get<&S::coverage>, &Self::set<&S::coverage>).category("Shape").order(10).slider(0.f, 1.f, 100)
		    .property("Density", &Self::get<&S::density>, &Self::set<&S::density>).category("Shape").order(11).slider(0.f, 1.f, 100)
		    .property("CloudType", &Self::get<&S::cloud_type>, &Self::set<&S::cloud_type>).category("Shape").order(12).slider(0.f, 1.f, 100)
		    .property("ShapeFactor", &Self::get<&S::shape_factor>, &Self::set<&S::shape_factor>).category("Shape").order(13).slider(0.f, 1.f, 100)
		    .property("ShapeScale", &Self::get<&S::shape_scale>, &Self::set<&S::shape_scale>).category("Shape").order(14).slider(0.1f, 20.f, 199)
		    .property("ErosionFactor", &Self::get<&S::erosion_factor>, &Self::set<&S::erosion_factor>).category("Shape").order(15).slider(0.f, 1.f, 100)
		    .property("ErosionScale", &Self::get<&S::erosion_scale>, &Self::set<&S::erosion_scale>).category("Shape").order(16).slider(1.f, 400.f, 399)
		    .property("BaseAltitude", &Self::get<&S::base_altitude>, &Self::set<&S::base_altitude>).category("Layer").order(20).slider(-10000.f, 200000.f, 2100)
		    .property("Thickness", &Self::get<&S::thickness>, &Self::set<&S::thickness>).category("Layer").order(21).slider(100.f, 50000.f, 499)
		    .property("HorizonFade", &Self::get<&S::horizon_fade>, &Self::set<&S::horizon_fade>).category("Layer").order(22).slider(1000.f, 1000000.f, 999, SliderScaling::Square)
		    .property("UseGlobalWind", &Self::get<&S::use_global_wind>, &Self::set<&S::use_global_wind>).category("Wind").order(30)
		    .property("GlobalWindScale", &Self::get<&S::global_wind_scale>, &Self::set<&S::global_wind_scale>).category("Wind").order(31).slider(0.f, 50.f, 500)
		    .property("WindDirection", &Self::get<&S::wind_direction>, &Self::set<&S::wind_direction>).category("Wind").order(32)
		    .property("WindSpeed", &Self::get<&S::wind_speed>, &Self::set<&S::wind_speed>).category("Wind").order(33).slider(0.f, 1000.f, 1000, SliderScaling::Square)
		    .property("Evolution", &Self::get<&S::evolution>, &Self::set<&S::evolution>).category("Wind").order(34).slider(0.f, 10.f, 100)
		    .property("Color", &Self::get<&S::color>, &Self::set<&S::color>).category("Lighting").order(40)
		    .property("SunIntensity", &Self::get<&S::sun_intensity>, &Self::set<&S::sun_intensity>).category("Lighting").order(41).slider(0.f, 4.f, 400)
		    .property("AmbientIntensity", &Self::get<&S::ambient_intensity>, &Self::set<&S::ambient_intensity>).category("Lighting").order(42).slider(0.f, 4.f, 400)
		    .property("Powder", &Self::get<&S::powder>, &Self::set<&S::powder>).category("Lighting").order(43).slider(0.f, 1.f, 100)
		    .property("MultiScattering", &Self::get<&S::multi_scattering>, &Self::set<&S::multi_scattering>).category("Lighting").order(44).slider(0.f, 1.f, 100)
		    .property("ShadowStrength", &Self::get<&S::shadow_strength>, &Self::set<&S::shadow_strength>).category("Lighting").order(45).slider(0.f, 1.f, 100)
		    .function("ApplyPreset", &Self::apply_preset)
		    .function("SetGlobalWind", &Self::set_global_wind)
		    .commit();
	}
}
