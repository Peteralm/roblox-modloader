#pragma once

#include "RobloxModLoader/util/layout_assert.hpp"

#include <cstddef>

namespace RBX::Graphics
{
	class Device;

	class VisualEngine
	{
	public:
		std::byte reserved_0[0x108];
		Device* device;
	};

	RML_LAYOUT_DIAGNOSTIC_PUSH()
	RML_ASSERT_OFFSET(VisualEngine, device, 0x108);
	RML_LAYOUT_DIAGNOSTIC_POP()
}
