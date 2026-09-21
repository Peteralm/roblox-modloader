#pragma once

#include "RobloxModLoader/util/layout_assert.hpp"

namespace RBX
{
	class Time
	{
	public:
		double seconds;
	};

	RML_LAYOUT_DIAGNOSTIC_PUSH()
	RML_ASSERT_SIZE(Time, 8);
	RML_LAYOUT_DIAGNOSTIC_POP()
}
