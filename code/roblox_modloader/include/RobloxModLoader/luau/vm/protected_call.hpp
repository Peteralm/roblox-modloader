#pragma once

#include "RobloxModLoader/rml_export.hpp"

#include <expected>
#include <string>

struct lua_State;

namespace rml::luau::vm
{
	using ProtectedFn = void (*)(lua_State*, void*);

	// Runs `fn` inside lua_pcall. Studio turns a Luau error that unwinds out of our C++ frames into
	// an unhandled lua_exception and kills the process, so every call that can raise - anything that
	// writes to a table Studio may have marked readonly - goes through here.
	RML_EXPORT std::expected<void, std::string> protected_call(lua_State* L, ProtectedFn fn, void* ctx,
	                                                           const char* debug_name) noexcept;
}
