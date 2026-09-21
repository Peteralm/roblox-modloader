#pragma once

#include "RobloxModLoader/rml_export.hpp"

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

	private:
		InitGate& m_gate;
	};
}
