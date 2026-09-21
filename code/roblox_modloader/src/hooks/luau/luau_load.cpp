#include "RobloxModLoader/hooking/hooking.hpp"
#include "RobloxModLoader/internal/common.hpp"
#include "RobloxModLoader/internal/function_types.hpp"
#include "RobloxModLoader/internal/hooking/engine_hooks.hpp"
#include "RobloxModLoader/luau/load_observer.hpp"
#include "RobloxModLoader/roblox/luau/roblox_extra_space.hpp"
#include "lstate.h"

lua_Status rml::Hooks::luau_load(lua_State* L, const char* chunkname, const char* data, size_t size, int env)
{
	luau::detail::dispatch_load(L, chunkname, data, size, env, luau::LoadPhase::Before, 0);
	const auto status = rml::Hooking::get_original<&rml::Hooks::luau_load>()(L, chunkname, data, size, env);
	luau::detail::dispatch_load(L, chunkname, data, size, env, luau::LoadPhase::After, status);
	return status;
}
