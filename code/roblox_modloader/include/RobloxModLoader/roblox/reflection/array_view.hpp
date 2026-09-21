#pragma once

#include <cstddef>

namespace RBX
{
	template<typename T>
	struct ArrayView
	{
		const T* data;
		std::size_t size;
	};
}
