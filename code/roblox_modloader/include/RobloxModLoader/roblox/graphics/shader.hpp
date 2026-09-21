#pragma once

#include "resource.hpp"

#include <cstdint>
#include <vector>

namespace RBX::Graphics
{
	class Shader : public Resource
	{
	public:
		enum class Type : std::uint32_t
		{
			Vertex,
			Fragment,
			Compute
		};

		virtual void reload(const std::vector<char>& bytecode) = 0;

		std::uint32_t type;
		std::uint32_t reserved_3c;
	};

	RML_LAYOUT_DIAGNOSTIC_PUSH()
	RML_ASSERT_OFFSET(Shader, type, 0x38);
	RML_ASSERT_SIZE(Shader, 0x40);
	RML_LAYOUT_DIAGNOSTIC_POP()

	class ShaderProgram : public Resource
	{
	public:
		virtual void reload() = 0;
	};
}
