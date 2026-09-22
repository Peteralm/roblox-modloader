#pragma once

#include "RobloxModLoader/util/layout_assert.hpp"

#include <atomic>
#include <cstdint>

namespace RBX
{
	template<typename T>
	class SubsystemHandle
	{
	public:
		const void* vtable;
		std::uint64_t lock;
		std::uint64_t pending;
		T* instance;
		std::atomic<std::uint32_t> state;

		T* get() const
		{
			return instance;
		}

		T* operator->() const
		{
			return instance;
		}

	private:
		SubsystemHandle() = delete;
	};

	RML_LAYOUT_DIAGNOSTIC_PUSH()
	RML_ASSERT_OFFSET(SubsystemHandle<int>, instance, 24);
	RML_ASSERT_OFFSET(SubsystemHandle<int>, state, 32);
	RML_LAYOUT_DIAGNOSTIC_POP()
}
