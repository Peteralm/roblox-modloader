#pragma once

#include "RobloxModLoader/rml_export.hpp"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <vector>

namespace rml::assets::mesh
{
	struct Vertex
	{
		float px, py, pz;
		float nx, ny, nz;
		float tu, tv;
		std::int8_t tx, ty, tz, ts;
		std::uint8_t r, g, b, a;
	};

	struct Face
	{
		std::uint32_t a, b, c;
	};

	struct Mesh
	{
		std::vector<Vertex> vertices;
		std::vector<Face> faces;
	};

	static_assert(sizeof(Vertex) == 40);
	static_assert(sizeof(Face) == 12);

	RML_EXPORT std::expected<Mesh, std::string> load_obj(const std::filesystem::path& path);
	RML_EXPORT std::vector<std::byte> encode_v2(const Mesh& mesh);
	RML_EXPORT std::expected<void, std::string> write_v2(const Mesh& mesh, const std::filesystem::path& path);
}
