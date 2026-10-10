#pragma once

#include "cloud_registry.hpp"
#include "cloud_settings.hpp"

#include <RobloxModLoader/mod/init_context.hpp>
#include <RobloxModLoader/roblox/reflection/described_creatable.hpp>

#include <type_traits>
#include <utility>

struct lua_State;

namespace clouds
{
	template<auto Member>
	using setting_t = std::remove_cvref_t<decltype(std::declval<CloudSettings&>().*Member)>;

	class VolumetricClouds final : public rml::reflection::DescribedCreatable<VolumetricClouds>
	{
	public:
		VolumetricClouds();
		~VolumetricClouds() override;

		template<auto Member>
		[[nodiscard]] setting_t<Member> get() const
		{
			return m_settings.*Member;
		}

		template<auto Member>
		void set(const setting_t<Member>& value)
		{
			m_settings.*Member = value;
			m_settings.clamp();
			CloudRegistry::instance().update(this, m_settings);
		}

		int apply_preset(lua_State* L);
		int set_global_wind(lua_State* L);
		void on_ancestor_changed(const RBX::AncestorChanged& change) override;

		static void define(rml::InitContext& context);

	private:
		[[nodiscard]] bool placed_in_world() const;

		CloudSettings m_settings;
	};
}
