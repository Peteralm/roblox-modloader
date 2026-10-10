#pragma once

#include "thing_enums.hpp"

#include <RobloxModLoader/roblox/reflection/described_creatable.hpp>
#include <RobloxModLoader/roblox/util/G3DCore.h>

#include <string>

namespace rml
{
	class InitContext;
}

struct lua_State;

class ModThing final : public rml::reflection::DescribedCreatable<ModThing>
{
public:
	float speed{};
	double volume{0.5};
	int count{};
	bool enabled{true};
	RBX::Color3 tint{1.f, 1.f, 1.f};
	RBX::Vector3 offset;
	std::string label{"ModThing"};
	std::string notes;
	int legacy{};
	bool debug{};
	ThingMode mode{};
	Face face{Face::Front};
	rbx::signal<void(float)> speed_reset;

	int reset(lua_State* L);
	void on_child_added(RBX::Instance* child) override;

	static void define(rml::InitContext& context);
};
