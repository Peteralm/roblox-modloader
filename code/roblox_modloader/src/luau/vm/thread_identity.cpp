#include "RobloxModLoader/luau/vm/thread_identity.hpp"

#include "RobloxModLoader/luau/generated/layout_access.hpp"
#include "RobloxModLoader/roblox/luau/roblox_extra_space.hpp"
#include "lobject.h"
#include "lstate.h"
#include "pointers.hpp"
#include "utils/seh_guard.hpp"

#include <memory>
#include <mutex>
#include <vector>

RML_LOG_SCOPE("ThreadIdentity");

namespace rml::luau::vm
{
	struct IdentityWrite
	{
		lua_State* raw_state;
		const mirror::LuaState* L;
		RBX::Security::Permissions identity;
		std::uint64_t capabilities;
		bool reflect_identity_number;
	};

	static void report_bad_identity_context(const void* context, const lua_State* L)
	{
		static std::once_flag once;
		std::call_once(once, [context, L] {
			RML_WARN("rbx_thread_identity_context returned 0x{:X} for state 0x{:X}, which is not a "
			         "ThreadIdentityContext; skipping the identity-number reflection on this build",
			    reinterpret_cast<std::uintptr_t>(context), reinterpret_cast<std::uintptr_t>(L));
		});
	}

	static void write_identity(void* ctx)
	{
		const auto* call = static_cast<IdentityWrite*>(ctx);

		if (auto* extra_space = static_cast<RBX::Luau::RobloxExtraSpace*>(call->L->userdata))
		{
			extra_space->context.identity = call->identity;
			extra_space->capabilities = call->capabilities;
		}

		if (!call->reflect_identity_number)
			return;

		const auto get_context = g_pointers ? g_pointers->m_roblox_pointers.rbx_thread_identity_context : nullptr;
		if (!get_context || !call->raw_state)
			return;

		auto* identity_context = static_cast<RBX::Luau::ThreadIdentityContext*>(get_context(call->raw_state));
		if (!identity_context)
			return;

		if (static_cast<void*>(identity_context) == call->L->userdata
		    || identity_context->bound_state != call->raw_state)
		{
			report_bad_identity_context(identity_context, call->raw_state);
			return;
		}

		identity_context->identity.identity = call->identity;
		identity_context->identity.asset_id = 0;
		identity_context->capabilities = call->capabilities;
	}

	static std::uint64_t* mask_for(const std::uint64_t capabilities)
	{
		static std::mutex guard;
		static std::vector<std::unique_ptr<std::uint64_t>> masks;

		const std::scoped_lock lock(guard);
		for (const auto& mask : masks)
			if (*mask == capabilities)
				return mask.get();

		return masks.emplace_back(std::make_unique<std::uint64_t>(capabilities)).get();
	}

	struct ElevateCall
	{
		const void* closure;
		std::uint64_t* mask;
	};

	static void set_proto(mirror::Proto* proto, std::uint64_t* mask)
	{
		if (!proto)
			return;

		proto->userdata = mask;

		if (proto->sizep <= 0 || !proto->p)
			return;

		for (int i = 0; i < proto->sizep; ++i)
			if (proto->p[i])
				set_proto(access::proto(proto->p[i]), mask);
	}

	static void elevate_proto_tree(void* ctx)
	{
		const auto* call = static_cast<ElevateCall*>(ctx);
		const auto* closure = access::closure(call->closure);
		if (closure->isC)
			return;

		set_proto(access::proto(closure->p), call->mask);
	}

	bool set_identity(lua_State* L, const RBX::Security::Permissions identity, const std::uint64_t capabilities,
	                  const bool reflect_identity_number) noexcept
	{
		if (!L)
		{
			RML_ERROR("Cannot set thread identity: Lua state is null");
			return false;
		}

		IdentityWrite call{L, access::state(L), identity, capabilities, reflect_identity_number};
		if (!utils::guarded_invoke(&write_identity, &call))
		{
			RML_ERROR("set_thread_identity faulted; skipping identity set");
			return false;
		}

		RML_INFO("Set thread identity to {} with capabilities 0x{:X}", static_cast<int>(identity), capabilities);
		return true;
	}

	struct ContextAccess
	{
		lua_State* state;
		RBX::Luau::ThreadIdentityContext* context;
	};

	static void resolve_context(void* ctx)
	{
		auto* call = static_cast<ContextAccess*>(ctx);
		call->context = nullptr;

		const auto get_context = g_pointers ? g_pointers->m_roblox_pointers.rbx_thread_identity_context : nullptr;
		if (!get_context || !call->state)
			return;

		auto* candidate = static_cast<RBX::Luau::ThreadIdentityContext*>(get_context(call->state));
		if (!candidate || candidate->bound_state != call->state)
		{
			report_bad_identity_context(candidate, call->state);
			return;
		}

		call->context = candidate;
	}

	struct ContextWrite
	{
		RBX::Luau::ThreadIdentityContext* context;
		RBX::Luau::ExtendedIdentity identity;
		std::uint64_t capabilities;
	};

	static void write_context(void* ctx)
	{
		const auto* call = static_cast<ContextWrite*>(ctx);
		call->context->identity = call->identity;
		call->context->capabilities = call->capabilities;
	}

	IdentitySnapshot elevate_scoped(lua_State* L, const RBX::Security::Permissions identity,
	                                const std::uint64_t capabilities) noexcept
	{
		IdentitySnapshot snapshot{};
		if (!L)
			return snapshot;

		// The extra space still has to carry the elevation: that is what RML's own
		// bindings read.
		set_identity(L, identity, capabilities, false);

		ContextAccess access_call{L, nullptr};
		if (!utils::guarded_invoke(&resolve_context, &access_call) || !access_call.context)
			return snapshot;

		snapshot.identity = access_call.context->identity;
		snapshot.capabilities = access_call.context->capabilities;
		snapshot.captured = true;

		ContextWrite write_call{access_call.context, {identity, 0}, capabilities};
		if (!utils::guarded_invoke(&write_context, &write_call))
		{
			RML_ERROR("Elevating the identity context faulted; the chunk runs unelevated");
			snapshot.captured = false;
		}

		return snapshot;
	}

	void restore_identity(lua_State* L, const IdentitySnapshot& snapshot) noexcept
	{
		if (!snapshot.captured || !L)
			return;

		ContextAccess access_call{L, nullptr};
		if (!utils::guarded_invoke(&resolve_context, &access_call) || !access_call.context)
			return;

		ContextWrite write_call{access_call.context, snapshot.identity, snapshot.capabilities};
		if (!utils::guarded_invoke(&write_context, &write_call))
		{
			RML_ERROR("Restoring the identity context faulted; it may stay elevated");
		}
	}

	RBX::Luau::TaskState task_state(lua_State*) noexcept
	{
		return RBX::Luau::TaskState::None;
	}

	void elevate_closure(const Closure* closure, const std::uint64_t capabilities) noexcept
	{
		if (!closure)
			return;

		ElevateCall call{closure, mask_for(capabilities)};
		if (!utils::guarded_invoke(&elevate_proto_tree, &call))
			RML_ERROR("elevate_closure faulted while walking the proto tree; capabilities were not applied");
	}

	void elevate_stack_closure(lua_State* L, const int index, const std::uint64_t capabilities) noexcept
	{
		const auto to_pointer = g_pointers ? g_pointers->m_roblox_pointers.lua_topointer : nullptr;
		if (!L || !to_pointer || !lua_isfunction(L, index))
			return;

		elevate_closure(static_cast<const Closure*>(to_pointer(L, index)), capabilities);
	}
}
