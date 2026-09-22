#include "RobloxModLoader/hooking/hooking.hpp"
#include "RobloxModLoader/internal/common.hpp"
#include "RobloxModLoader/internal/hooking/engine_hooks.hpp"
#include "app/init_gate.hpp"

RML_LOG_SCOPE("GlobalInitHook");

void rml::Hooks::global_init()
{
	if (const auto gate = InitGate::instance())
		gate->on_global_init_reached();

	Hooking::get_original<&Hooks::global_init>()();
}
