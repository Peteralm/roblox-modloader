#pragma once

#include "RobloxModLoader/util/layout_assert.hpp"

#include <cstdint>
#include <string>

namespace RBX
{
	namespace detail
	{
		template<typename T>
		class FlyweightData
		{
		public:
			std::uint32_t integrity;
			T value;
			std::uint64_t hash;
			std::uint32_t references;
			std::uint32_t reserved_2c;
		};

		RML_LAYOUT_DIAGNOSTIC_PUSH()
		RML_ASSERT_OFFSET(FlyweightData<std::string>, value, 0x8);
		// Everything past `value` moves with the standard library's own std::string, which is
		// 0x20 on the MSVC one Studio is built with and 0x18 on libc++.
#if defined(RML_WINDOWS)
		RML_ASSERT_OFFSET(FlyweightData<std::string>, hash, 0x28);
		RML_ASSERT_OFFSET(FlyweightData<std::string>, references, 0x30);
		RML_ASSERT_SIZE(FlyweightData<std::string>, 0x38);
#else
		RML_ASSERT_OFFSET(FlyweightData<std::string>, hash, 0x20);
		RML_ASSERT_OFFSET(FlyweightData<std::string>, references, 0x28);
		RML_ASSERT_SIZE(FlyweightData<std::string>, 0x30);
#endif
		RML_LAYOUT_DIAGNOSTIC_POP()
	}

	template<typename T>
	class Flyweight
	{
	public:
		const detail::FlyweightData<T>* data;

		const T& value() const
		{
			return data->value;
		}
	};
}
