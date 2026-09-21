#include "RobloxModLoader/luau/load_observer.hpp"

#include <array>
#include <atomic>

namespace rml::luau
{
	namespace
	{
		constexpr std::size_t kObserverCapacity = 16;
		std::array<std::atomic<LoadObserver>, kObserverCapacity> s_observers{};
	}

	bool register_load_observer(const LoadObserver observer) noexcept
	{
		if (!observer)
			return false;
		for (const auto& slot : s_observers)
			if (slot.load(std::memory_order_acquire) == observer)
				return true;
		for (auto& slot : s_observers)
		{
			LoadObserver expected{};
			if (slot.compare_exchange_strong(expected, observer, std::memory_order_acq_rel))
				return true;
		}
		return false;
	}

	bool unregister_load_observer(const LoadObserver observer) noexcept
	{
		if (!observer)
			return false;
		for (auto& slot : s_observers)
		{
			auto expected = observer;
			if (slot.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel))
				return true;
		}
		return false;
	}

	namespace detail
	{
		void dispatch_load(lua_State* state, const char* chunk_name, const char* source, const std::size_t source_size, const int environment, const LoadPhase phase, const int status) noexcept
		{
			for (const auto& slot : s_observers)
				if (const auto observer = slot.load(std::memory_order_acquire))
					observer(state, chunk_name, source, source_size, environment, phase, status);
		}
	}
} // namespace rml::luau
