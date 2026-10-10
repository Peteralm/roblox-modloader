#include "noise_volumes.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <fstream>
#include <functional>
#include <thread>

namespace clouds
{
	static constexpr std::uint32_t k_cache_magic = 0x434C4D52;
	static constexpr std::uint32_t k_cache_version = 3;

	static std::uint32_t hash(const std::uint32_t x, const std::uint32_t y, const std::uint32_t z, const std::uint32_t salt)
	{
		std::uint32_t h = x * 0x8da6b343u + y * 0xd8163841u + z * 0xcb1ab31fu + salt * 0x9e3779b9u;
		h ^= h >> 16;
		h *= 0x7feb352du;
		h ^= h >> 15;
		h *= 0x846ca68bu;
		h ^= h >> 16;
		return h;
	}

	static float unit(const std::uint32_t h)
	{
		return static_cast<float>(h >> 8) * (1.f / 16777216.f);
	}

	static std::uint32_t wrap(const int value, const int period)
	{
		const int m = value % period;
		return static_cast<std::uint32_t>(m < 0 ? m + period : m);
	}

	static float remap(const float v, const float l0, const float h0, const float l1, const float h1)
	{
		return l1 + (v - l0) * (h1 - l1) / (h0 - l0);
	}

	static float worley(const float x, const float y, const float z, const int period, const std::uint32_t salt)
	{
		const float px = x * static_cast<float>(period);
		const float py = y * static_cast<float>(period);
		const float pz = z * static_cast<float>(period);
		const int cx = static_cast<int>(std::floor(px));
		const int cy = static_cast<int>(std::floor(py));
		const int cz = static_cast<int>(std::floor(pz));
		float best = 1e9f;
		for (int dz = -1; dz <= 1; ++dz)
			for (int dy = -1; dy <= 1; ++dy)
				for (int dx = -1; dx <= 1; ++dx)
				{
					const auto h = hash(wrap(cx + dx, period), wrap(cy + dy, period), wrap(cz + dz, period), salt);
					const float fx = static_cast<float>(cx + dx) + unit(h) - px;
					const float fy = static_cast<float>(cy + dy) + unit(hash(h, 1, 0, salt)) - py;
					const float fz = static_cast<float>(cz + dz) + unit(hash(h, 2, 0, salt)) - pz;
					best = std::min(best, fx * fx + fy * fy + fz * fz);
				}
		return 1.f - std::min(std::sqrt(best), 1.f);
	}

	static float gradient(const std::uint32_t h, const float x, const float y, const float z)
	{
		switch (h & 15)
		{
		case 0: return x + y;
		case 1: return -x + y;
		case 2: return x - y;
		case 3: return -x - y;
		case 4: return x + z;
		case 5: return -x + z;
		case 6: return x - z;
		case 7: return -x - z;
		case 8: return y + z;
		case 9: return -y + z;
		case 10: return y - z;
		case 11: return -y - z;
		case 12: return x + y;
		case 13: return -y + z;
		case 14: return -x + y;
		default: return -y - z;
		}
	}

	static float fade(const float t)
	{
		return t * t * t * (t * (t * 6.f - 15.f) + 10.f);
	}

	static float lerp(const float a, const float b, const float t)
	{
		return a + (b - a) * t;
	}

	static float perlin(const float x, const float y, const float z, const int period, const std::uint32_t salt)
	{
		const float px = x * static_cast<float>(period);
		const float py = y * static_cast<float>(period);
		const float pz = z * static_cast<float>(period);
		const int ix = static_cast<int>(std::floor(px));
		const int iy = static_cast<int>(std::floor(py));
		const int iz = static_cast<int>(std::floor(pz));
		const float fx = px - static_cast<float>(ix);
		const float fy = py - static_cast<float>(iy);
		const float fz = pz - static_cast<float>(iz);
		const auto corner = [&](const int ox, const int oy, const int oz) {
			const auto h = hash(wrap(ix + ox, period), wrap(iy + oy, period), wrap(iz + oz, period), salt);
			return gradient(h, fx - static_cast<float>(ox), fy - static_cast<float>(oy), fz - static_cast<float>(oz));
		};
		const float u = fade(fx);
		const float v = fade(fy);
		const float w = fade(fz);
		return lerp(lerp(lerp(corner(0, 0, 0), corner(1, 0, 0), u), lerp(corner(0, 1, 0), corner(1, 1, 0), u), v),
		    lerp(lerp(corner(0, 0, 1), corner(1, 0, 1), u), lerp(corner(0, 1, 1), corner(1, 1, 1), u), v), w);
	}

	static float perlin_fbm(const float x, const float y, const float z, const int period, const int octaves, const std::uint32_t salt)
	{
		float sum = 0.f;
		float amplitude = 1.f;
		float norm = 0.f;
		for (int octave = 0; octave < octaves; ++octave)
		{
			sum += perlin(x, y, z, period << octave, salt + static_cast<std::uint32_t>(octave)) * amplitude;
			norm += amplitude;
			amplitude *= 0.5f;
		}
		return std::clamp(sum / norm * 0.75f + 0.5f, 0.f, 1.f);
	}

	static void parallel_for(const std::uint32_t count, const std::function<void(std::uint32_t)>& body)
	{
		std::atomic<std::uint32_t> next{0};
		std::vector<std::jthread> workers;
		const auto threads = std::max(1u, std::thread::hardware_concurrency());
		for (unsigned i = 0; i < threads; ++i)
			workers.emplace_back([&] {
				for (auto item = next++; item < count; item = next++)
					body(item);
			});
	}

	static std::uint8_t to_byte(const float value)
	{
		return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.f, 1.f) * 255.f));
	}

	static std::vector<std::uint8_t> downsample(const std::vector<std::uint8_t>& source, const std::uint32_t size, const std::uint32_t depth, const std::uint32_t channels)
	{
		const auto half = std::max(size / 2, 1u);
		const auto half_depth = std::max(depth / 2, 1u);
		const auto depth_taps = depth > 1 ? 2u : 1u;
		std::vector<std::uint8_t> result(static_cast<std::size_t>(half) * half * half_depth * channels);
		for (std::uint32_t z = 0; z < half_depth; ++z)
			for (std::uint32_t y = 0; y < half; ++y)
				for (std::uint32_t x = 0; x < half; ++x)
					for (std::uint32_t c = 0; c < channels; ++c)
					{
						std::uint32_t sum = 0;
						std::uint32_t taps = 0;
						for (std::uint32_t dz = 0; dz < depth_taps; ++dz)
							for (std::uint32_t dy = 0; dy < 2; ++dy)
								for (std::uint32_t dx = 0; dx < 2; ++dx)
								{
									const auto sx = std::min(x * 2 + dx, size - 1);
									const auto sy = std::min(y * 2 + dy, size - 1);
									const auto sz = std::min(z * depth_taps + dz, depth - 1);
									sum += source[((static_cast<std::size_t>(sz) * size + sy) * size + sx) * channels + c];
									++taps;
								}
						result[((static_cast<std::size_t>(z) * half + y) * half + x) * channels + c] = static_cast<std::uint8_t>((sum + taps / 2) / taps);
					}
		return result;
	}

	static void stretch(std::vector<std::uint8_t>& texels, const std::uint32_t channels)
	{
		for (std::uint32_t c = 0; c < channels; ++c)
		{
			std::array<std::size_t, 256> histogram{};
			for (std::size_t i = c; i < texels.size(); i += channels)
				++histogram[texels[i]];

			const auto count = texels.size() / channels;
			std::size_t seen = 0;
			int low = 0;
			while (low < 255 && (seen += histogram[low]) < count / 100)
				++low;
			seen = 0;
			int high = 255;
			while (high > low && (seen += histogram[high]) < count / 100)
				--high;
			if (high <= low)
				continue;

			for (std::size_t i = c; i < texels.size(); i += channels)
				texels[i] = to_byte(static_cast<float>(texels[i] - low) / static_cast<float>(high - low));
		}
	}

	static NoiseTexture build(const std::uint32_t size, const std::uint32_t depth, const std::uint32_t channels, const std::function<void(float, float, float, std::uint8_t*)>& texel)
	{
		NoiseTexture texture{size, depth, channels, {}};
		std::vector<std::uint8_t> base(static_cast<std::size_t>(size) * size * depth * channels);
		parallel_for(depth, [&](const std::uint32_t z) {
			for (std::uint32_t y = 0; y < size; ++y)
				for (std::uint32_t x = 0; x < size; ++x)
				{
					const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(size);
					const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(size);
					const float w = (static_cast<float>(z) + 0.5f) / static_cast<float>(depth);
					texel(u, v, w, &base[((static_cast<std::size_t>(z) * size + y) * size + x) * channels]);
				}
		});
		stretch(base, channels);
		texture.mips.push_back(std::move(base));
		for (auto level_size = size, level_depth = depth; level_size > 1 || level_depth > 1;)
		{
			texture.mips.push_back(downsample(texture.mips.back(), level_size, level_depth, channels));
			level_size = std::max(level_size / 2, 1u);
			level_depth = std::max(level_depth / 2, 1u);
		}
		return texture;
	}

	static NoiseSet generate()
	{
		NoiseSet set;
		set.shape = build(128, 128, 1, [](const float x, const float y, const float z, std::uint8_t* out) {
			const float low = worley(x, y, z, 6, 21) * 0.625f + worley(x, y, z, 12, 22) * 0.25f + worley(x, y, z, 24, 23) * 0.125f;
			const float perlin_worley = remap(perlin_fbm(x, y, z, 4, 4, 11), 0.f, 1.f, low, 1.f);
			const float fbm = worley(x, y, z, 8, 31) * 0.625f + worley(x, y, z, 16, 32) * 0.25f + worley(x, y, z, 32, 33) * 0.125f;
			out[0] = to_byte(remap(perlin_worley, fbm - 1.f, 1.f, 0.f, 1.f));
		});
		set.detail = build(64, 64, 1, [](const float x, const float y, const float z, std::uint8_t* out) {
			out[0] = to_byte(worley(x, y, z, 4, 41) * 0.625f + worley(x, y, z, 8, 42) * 0.25f + worley(x, y, z, 16, 43) * 0.125f);
		});
		set.weather = build(512, 1, 2, [](const float x, const float y, float, std::uint8_t* out) {
			const float field = perlin_fbm(x, y, 0.f, 4, 5, 51) * 0.6f + worley(x, y, 0.f, 8, 52) * 0.6f - 0.1f;
			out[0] = to_byte(field);
			out[1] = to_byte(perlin_fbm(x, y, 0.5f, 2, 3, 61));
		});
		return set;
	}

	static void write_texture(std::ofstream& file, const NoiseTexture& texture)
	{
		const std::array<std::uint32_t, 4> header{texture.size, texture.depth, texture.channels, static_cast<std::uint32_t>(texture.mips.size())};
		file.write(reinterpret_cast<const char*>(header.data()), sizeof(header));
		for (const auto& mip : texture.mips)
		{
			const auto bytes = static_cast<std::uint32_t>(mip.size());
			file.write(reinterpret_cast<const char*>(&bytes), sizeof(bytes));
			file.write(reinterpret_cast<const char*>(mip.data()), bytes);
		}
	}

	static bool read_texture(std::ifstream& file, NoiseTexture& texture)
	{
		std::array<std::uint32_t, 4> header{};
		if (!file.read(reinterpret_cast<char*>(header.data()), sizeof(header)) || header[3] == 0 || header[3] > 16)
			return false;
		texture = {header[0], header[1], header[2], std::vector<std::vector<std::uint8_t>>(header[3])};
		for (auto& mip : texture.mips)
		{
			std::uint32_t bytes = 0;
			if (!file.read(reinterpret_cast<char*>(&bytes), sizeof(bytes)) || bytes > 64u * 1024 * 1024)
				return false;
			mip.resize(bytes);
			if (!file.read(reinterpret_cast<char*>(mip.data()), bytes))
				return false;
		}
		return true;
	}

	static bool matches(const NoiseTexture& texture, const std::uint32_t size, const std::uint32_t depth, const std::uint32_t channels)
	{
		if (texture.size != size || texture.depth != depth || texture.channels != channels)
			return false;

		std::size_t level = 0;
		for (auto level_size = size, level_depth = depth;; ++level)
		{
			if (level >= texture.mips.size() || texture.mips[level].size() != static_cast<std::size_t>(level_size) * level_size * level_depth * channels)
				return false;
			if (level_size == 1 && level_depth == 1)
				break;
			level_size = std::max(level_size / 2, 1u);
			level_depth = std::max(level_depth / 2, 1u);
		}
		return level + 1 == texture.mips.size();
	}

	static bool load(const std::filesystem::path& path, NoiseSet& set)
	{
		std::ifstream file(path, std::ios::binary);
		std::array<std::uint32_t, 2> header{};
		if (!file || !file.read(reinterpret_cast<char*>(header.data()), sizeof(header)) || header[0] != k_cache_magic || header[1] != k_cache_version)
			return false;
		return read_texture(file, set.shape) && read_texture(file, set.detail) && read_texture(file, set.weather) && matches(set.shape, 128, 128, 1) &&
		    matches(set.detail, 64, 64, 1) && matches(set.weather, 512, 1, 2);
	}

	static void save(const std::filesystem::path& path, const NoiseSet& set)
	{
		auto temporary = path;
		temporary += ".tmp";
		{
			std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
			const std::array<std::uint32_t, 2> header{k_cache_magic, k_cache_version};
			file.write(reinterpret_cast<const char*>(header.data()), sizeof(header));
			write_texture(file, set.shape);
			write_texture(file, set.detail);
			write_texture(file, set.weather);
			if (!file)
				return;
		}
		std::error_code error;
		std::filesystem::rename(temporary, path, error);
	}

	NoiseSet load_or_generate_noise(const std::filesystem::path& cache_file)
	{
		NoiseSet set;
		if (load(cache_file, set))
			return set;
		set = generate();
		save(cache_file, set);
		return set;
	}
}
