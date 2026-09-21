#include "RobloxModLoader/hooking/hooking.hpp"
#include "RobloxModLoader/internal/common.hpp"
#include "RobloxModLoader/internal/hooking/engine_hooks.hpp"
#include "roblox/reflection/class_registry.hpp"

const RBX::ICreator* rml::Hooks::creatable_get_creator(const RBX::Name* name)
{
	if (const auto creator = reflection::ClassRegistry::instance().creator_for(name))
		return creator;

	return Hooking::get_original<&Hooks::creatable_get_creator>()(name);
}
