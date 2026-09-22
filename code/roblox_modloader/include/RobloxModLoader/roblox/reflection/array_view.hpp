#pragma once

#include <span>

namespace RBX
{
	template<typename T>
	using ArrayView = std::span<const T>;
}
