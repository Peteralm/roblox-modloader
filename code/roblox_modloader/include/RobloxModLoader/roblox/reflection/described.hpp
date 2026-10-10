#pragma once

#include "RobloxModLoader/util/compile_time.hpp"

#include <string_view>

namespace RBX
{
	template<typename Self, typename Base, rml::utils::fixed_string Name>
	class Described : public Base
	{
	public:
		static constexpr std::string_view class_name = Name.view();

	protected:
		Described()
		{
		}

	public:
		~Described() override
		{
		}
	};
}
