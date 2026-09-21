#include "RobloxModLoader/hooking/hooking.hpp"
#include "RobloxModLoader/internal/common.hpp"
#include "RobloxModLoader/internal/hooking/engine_hooks.hpp"
#include "roblox/reflection/class_registry.hpp"

void rml::Hooks::instance_dtor(void* self)
{
	reflection::ClassRegistry::instance().forget(self);
	Hooking::get_original<&Hooks::instance_dtor>()(self);
}
