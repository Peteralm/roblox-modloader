#pragma once

#include "RobloxModLoader/util/layout_assert.hpp"

#include <cstddef>

namespace RBX::Graphics
{
	class RenderCamera
	{
	public:
		float view[16];
		float projection[16];
		float view_projection[16];
	};

	RML_LAYOUT_DIAGNOSTIC_PUSH()
	RML_ASSERT_OFFSET(RenderCamera, projection, 0x40);
	RML_ASSERT_OFFSET(RenderCamera, view_projection, 0x80);
	RML_LAYOUT_DIAGNOSTIC_POP()
}
