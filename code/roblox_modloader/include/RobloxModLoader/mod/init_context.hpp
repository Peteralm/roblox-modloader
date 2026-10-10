#pragma once

#include "RobloxModLoader/rml_export.hpp"
#include "RobloxModLoader/roblox/reflection/class_builder.hpp"
#include "RobloxModLoader/roblox/reflection/enum_builder.hpp"

#include <string_view>

namespace rml
{
	class InitGate;

	class RML_EXPORT InitContext
	{
	public:
		explicit InitContext(InitGate& gate) :
		    m_gate(gate)
		{
		}

		[[nodiscard]] bool is_open() const;

		template<typename Derived>
		[[nodiscard]] reflection::TypedClassBuilder<Derived> define_class(std::string_view name, std::string_view base = "Instance")
		{
			return reflection::TypedClassBuilder<Derived>(name, base);
		}

		template<reflection::ModEnum E>
		[[nodiscard]] reflection::TypedEnumBuilder<E> define_enum(std::string_view name)
		{
			return reflection::TypedEnumBuilder<E>(name, false);
		}

		template<reflection::ModEnum E>
		[[nodiscard]] reflection::TypedEnumBuilder<E> bind_enum(std::string_view name)
		{
			return reflection::TypedEnumBuilder<E>(name, true);
		}

		template<typename Base = RBX::Instance>
		[[nodiscard]] reflection::TypedExtensionBuilder<Base> extend_class(std::string_view name)
		{
			return reflection::TypedExtensionBuilder<Base>(name);
		}

	private:
		InitGate& m_gate;
	};
}
