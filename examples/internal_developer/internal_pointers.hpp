#pragma once

#include "RobloxModLoader/memory/module.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace internal_developer
{
	inline constexpr std::size_t max_internal_flags = 4;

	struct EnginePointers
	{
		void* is_internal{nullptr};
		std::array<bool*, max_internal_flags> flags{};
		std::size_t flag_count{0};

		bool add_flag(bool* const flag) noexcept
		{
			if (flag == nullptr || flag_count == flags.size())
				return false;

			for (std::size_t index = 0; index < flag_count; ++index)
			{
				if (flags[index] == flag)
					return false;
			}

			flags[flag_count++] = flag;
			return true;
		}

		[[nodiscard]] bool complete() const noexcept
		{
			return is_internal != nullptr && flag_count != 0;
		}
	};

	[[nodiscard]] EnginePointers& engine_pointers() noexcept;

	[[nodiscard]] bool resolve_engine_pointers();
}
