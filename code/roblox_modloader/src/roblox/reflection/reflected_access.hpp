#pragma once

#include "type_index.hpp"

#include "RobloxModLoader/roblox/instance.hpp"
#include "RobloxModLoader/roblox/reflection/property.hpp"
#include "RobloxModLoader/roblox/reflection/property_descriptor.hpp"
#include "RobloxModLoader/roblox/reflection/reflected.hpp"

#include <concepts>
#include <expected>
#include <format>
#include <string>
#include <type_traits>

namespace rml::reflection
{
	template<typename T>
	inline constexpr const char* engine_type_name_v = nullptr;

	template<>
	inline constexpr const char* engine_type_name_v<double> = "double";

	template<>
	inline constexpr const char* engine_type_name_v<int> = "int";

	template<>
	inline constexpr const char* engine_type_name_v<bool> = "bool";

	template<>
	inline constexpr const char* engine_type_name_v<std::string> = "string";

	template<typename Object, typename Declaring, typename T, utils::fixed_string Name>
	    requires std::derived_from<Object, Declaring> && std::derived_from<Object, RBX::Instance>
	std::expected<void, std::string> assign(Object& object, RBX::Reflection::Reflected<T, Name> Declaring::*, const std::type_identity_t<T>& value)
	{
		static_assert(engine_type_name_v<T> != nullptr, "this field type has no engine reflection type");
		const auto* descriptor = object.get_descriptor().find_property(Name.c_str());
		if (!descriptor || &descriptor->type != TypeIndex::find(engine_type_name_v<T>))
			return std::unexpected(std::format("field {} is missing or not a {} on this build", Name.view(), engine_type_name_v<T>));
		try
		{
			RBX::Property(*descriptor, &object).template set<T>(value);
			return {};
		}
		catch (const std::exception& e)
		{
			return std::unexpected(std::format("field {} rejected the value: {}", Name.view(), e.what()));
		}
		catch (...)
		{
			return std::unexpected(std::format("field {} rejected the value", Name.view()));
		}
	}

	template<typename Object, typename Declaring, typename T, utils::fixed_string Name>
	    requires std::derived_from<Object, Declaring> && std::derived_from<Object, RBX::Instance>
	bool mirrors(const Object& object, RBX::Reflection::Reflected<T, Name> Declaring::* field)
	{
		static_assert(engine_type_name_v<T> != nullptr, "this field type has no engine reflection type");
		const auto* descriptor = object.get_descriptor().find_property(Name.c_str());
		if (!descriptor || &descriptor->type != TypeIndex::find(engine_type_name_v<T>))
			return false;
		try
		{
			return RBX::ConstProperty(*descriptor, &object).template get<T>() == (object.*field).get();
		}
		catch (...)
		{
			return false;
		}
	}
}
