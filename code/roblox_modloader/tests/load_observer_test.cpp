#include "RobloxModLoader/luau/load_observer.hpp"

#include <array>
#include <cstddef>
#include <doctest/doctest.h>

namespace rml::luau
{
	namespace
	{
		std::array<int, 17> s_calls{};
		LoadPhase s_phase{};
		int s_status{};

		template<std::size_t Index>
		void observer(lua_State*, const char*, const char*, std::size_t, int, const LoadPhase phase, const int status) noexcept
		{
			++s_calls[Index];
			s_phase = phase;
			s_status = status;
		}

		constexpr std::array<LoadObserver, 17> kObservers{observer<0>, observer<1>, observer<2>, observer<3>, observer<4>, observer<5>, observer<6>, observer<7>, observer<8>, observer<9>, observer<10>, observer<11>, observer<12>, observer<13>, observer<14>, observer<15>, observer<16>};
	}

	TEST_CASE("Luau load observers dispatch fixed bounded callbacks")
	{
		s_calls.fill(0);
		for (std::size_t index = 0; index < 16; ++index)
			REQUIRE(register_load_observer(kObservers[index]));
		CHECK(register_load_observer(kObservers[0]));
		CHECK_FALSE(register_load_observer(kObservers[16]));

		detail::dispatch_load(nullptr, "=CommandLine", "print(1)", 8, 0, LoadPhase::Before, 0);
		for (std::size_t index = 0; index < 16; ++index)
			CHECK(s_calls[index] == 1);
		CHECK(s_phase == LoadPhase::Before);
		detail::dispatch_load(nullptr, "=CommandLine", "print(1)", 8, 0, LoadPhase::After, 7);
		CHECK(s_phase == LoadPhase::After);
		CHECK(s_status == 7);

		for (std::size_t index = 0; index < 16; ++index)
			CHECK(unregister_load_observer(kObservers[index]));
		CHECK_FALSE(unregister_load_observer(kObservers[0]));
	}
} // namespace rml::luau
