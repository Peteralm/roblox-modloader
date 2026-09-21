#pragma once

#include "RobloxModLoader/rml_export.hpp"

#include <string_view>

namespace rml
{
	class InitGate;

	namespace reflection
	{
		class ClassBuilder;
	}

	class RML_EXPORT InitContext
	{
	public:
		explicit InitContext(InitGate& gate) :
		    m_gate(gate)
		{
		}

		[[nodiscard]] bool is_open() const;
		[[nodiscard]] reflection::ClassBuilder define_class(std::string_view name, std::string_view base = "Instance");

	private:
		InitGate& m_gate;
	};
}
