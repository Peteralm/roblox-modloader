#pragma once

#include "RobloxModLoader/util/compile_time.hpp"

#include <string_view>

namespace RBX::Reflection
{
	template<typename T, rml::utils::fixed_string Name>
	class Reflected
	{
	public:
		using value_type = T;
		static constexpr std::string_view name = Name.view();

		[[nodiscard]] const T& get() const
		{
			return m_value;
		}

		operator const T&() const
		{
			return m_value;
		}

	private:
		T m_value;
	};
}
