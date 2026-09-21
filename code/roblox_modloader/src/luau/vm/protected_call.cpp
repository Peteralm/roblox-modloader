#include "RobloxModLoader/luau/vm/protected_call.hpp"

#include "RobloxModLoader/luau/vm/stack_guard.hpp"
#include "RobloxModLoader/luau/vm/vm_api.hpp"

#include <cstring>

namespace rml::luau::vm
{
	namespace
	{
		struct Call
		{
			ProtectedFn fn;
			void* ctx;
		};

		int trampoline(lua_State* L)
		{
			std::size_t length = 0;
			const auto* bytes = lua_tolstring(L, lua_upvalueindex(1), &length);

			if (bytes == nullptr || length != sizeof(Call*))
			{
				return 0;
			}

			Call* call = nullptr;
			std::memcpy(&call, bytes, sizeof(call));
			call->fn(L, call->ctx);

			return 0;
		}
	}

	std::expected<void, std::string> protected_call(lua_State* L, const ProtectedFn fn, void* ctx, const char* debug_name) noexcept
	{
		if (L == nullptr || fn == nullptr)
		{
			return std::unexpected(std::string{"no state or function to call"});
		}

		if (!api_ready())
		{
			return std::unexpected(std::string{"the Luau C API is unavailable on this Studio build"});
		}

		Call call{fn, ctx};
		auto* pointer = &call;

		lua_pushlstring(L, reinterpret_cast<const char*>(&pointer), sizeof(pointer));
		lua_pushcclosure(L, &trampoline, debug_name, 1);

		if (lua_pcall(L, 0, 0, 0) == LUA_OK)
		{
			return {};
		}

		std::size_t length = 0;
		const auto* message = lua_tolstring(L, -1, &length);
		std::string text = message ? std::string(message, length) : std::string{"<no message>"};
		lua_pop(L, 1);

		return std::unexpected(std::move(text));
	}
}
