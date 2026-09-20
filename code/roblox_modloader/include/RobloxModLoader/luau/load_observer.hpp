#pragma once

#include "RobloxModLoader/rml_export.hpp"

#include <cstddef>

struct lua_State;

namespace rml::luau
{
	enum class LoadPhase { Before, After };
	using LoadObserver = void (*)(lua_State* state, const char* chunk_name, const char* source,
	    std::size_t source_size, int environment, LoadPhase phase, int status) noexcept;

	[[nodiscard]] RML_EXPORT bool register_load_observer(LoadObserver observer) noexcept;
	[[nodiscard]] RML_EXPORT bool unregister_load_observer(LoadObserver observer) noexcept;

	namespace detail
	{
		void dispatch_load(lua_State* state, const char* chunk_name, const char* source,
		    std::size_t source_size, int environment, LoadPhase phase, int status) noexcept;
	}
} // namespace rml::luau
