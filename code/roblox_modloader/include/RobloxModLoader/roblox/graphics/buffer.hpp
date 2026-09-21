#pragma once

#include "resource.hpp"

#include <cstdint>
#include <vector>

namespace RBX::Graphics
{
	class Buffer : public Resource
	{
	public:
		enum class Type : std::uint32_t
		{
			Vertex,
			Index,
			Constant,
			Structured
		};

		enum class Usage : std::uint32_t
		{
			Static,
			Dynamic
		};

		virtual void* lock() = 0;
		virtual void unlock(unsigned size) = 0;
		virtual void upload(unsigned offset, const void* data, unsigned size) = 0;
		virtual void download_debug(unsigned offset, void* data, unsigned size) = 0;

		std::uint32_t type;
		std::uint32_t size;
		std::uint32_t element_size;
		std::uint32_t usage;
	};

	RML_LAYOUT_DIAGNOSTIC_PUSH()
	RML_ASSERT_OFFSET(Buffer, type, 0x38);
	RML_ASSERT_OFFSET(Buffer, usage, 0x44);
	RML_ASSERT_SIZE(Buffer, 0x48);
	RML_LAYOUT_DIAGNOSTIC_POP()

	class VertexLayout : public Resource
	{
	public:
		struct Element
		{
			unsigned stream;
			unsigned offset;
			unsigned format;
			unsigned semantic;
			unsigned semantic_index;
		};
	};

	class Geometry : public Resource
	{
	public:
		enum class Primitive : std::uint32_t
		{
			Triangles,
			Lines,
			Points,
			TriangleStrip
		};
	};
}
