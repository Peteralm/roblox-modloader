#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace clouds
{
	struct NoiseTexture
	{
		std::uint32_t size{};
		std::uint32_t depth{};
		std::uint32_t channels{};
		std::vector<std::vector<std::uint8_t>> mips;
	};

	struct NoiseSet
	{
		NoiseTexture shape;
		NoiseTexture detail;
		NoiseTexture weather;
	};

	[[nodiscard]] NoiseSet load_or_generate_noise(const std::filesystem::path& cache_file);
}
