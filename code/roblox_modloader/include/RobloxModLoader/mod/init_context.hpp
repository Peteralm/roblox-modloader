#pragma once

#include "RobloxModLoader/rml_export.hpp"
#include "RobloxModLoader/roblox/reflection/class_builder.hpp"

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

	private:
		InitGate& m_gate;
	};
}
