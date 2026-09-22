#pragma once

#include "RobloxModLoader/internal/common.hpp"
#include "RobloxModLoader/roblox/luau/roblox_extra_space.hpp"
#include "RobloxModLoader/roblox/security/script_permissions.hpp"

struct Closure;

namespace rml::luau::vm
{
	bool set_identity(lua_State* L, RBX::Security::Permissions identity, std::uint64_t capabilities,
	                  bool reflect_identity_number = true) noexcept;

	/// What the engine had in a thread's identity context before RML touched it.
	struct IdentitySnapshot
	{
		RBX::Luau::ExtendedIdentity identity{};
		std::uint64_t capabilities{};
		bool captured{};
	};

	/// Elevates a thread the way Studio reads it (the identity context, not only the
	/// extra space) and hands back what was there. Identity contexts are recycled
	/// between threads, so an elevation that is never restored leaks the privilege
	/// into whatever thread receives that context next.
	[[nodiscard]] IdentitySnapshot elevate_scoped(lua_State* L, RBX::Security::Permissions identity,
	                                              std::uint64_t capabilities) noexcept;

	void restore_identity(lua_State* L, const IdentitySnapshot& snapshot) noexcept;

	[[nodiscard]] RBX::Luau::TaskState task_state(lua_State* L) noexcept;

	void elevate_closure(const Closure* closure, std::uint64_t capabilities) noexcept;

	void elevate_stack_closure(lua_State* L, int index, std::uint64_t capabilities) noexcept;
}
