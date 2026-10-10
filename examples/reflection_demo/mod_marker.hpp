#pragma once

#include <RobloxModLoader/roblox/reflection/described_creatable.hpp>

#include <string>

namespace rml
{
	class InitContext;
}

class ModMarker final : public rml::reflection::DescribedCreatable<ModMarker>
{
public:
	std::string tag;

	bool ask_set_parent(const RBX::Instance* parent) const override;

	static void define(rml::InitContext& context);
};
