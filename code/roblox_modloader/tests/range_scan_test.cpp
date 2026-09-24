#include <doctest/doctest.h>

#if defined(_M_X64) || defined(__x86_64__)

	#include "RobloxModLoader/memory/range.hpp"

	#include <array>
	#include <cstdint>
	#include <cstring>

namespace
{
	constexpr std::size_t k_size = 40;
	constexpr std::size_t k_target = 16;

	// lea r64, [rip + disp32] at `offset`, aimed at buffer[k_target].
	void put_lea(std::array<std::uint8_t, k_size>& buffer, const std::size_t offset, const std::uint8_t rex, const std::uint8_t modrm)
	{
		buffer[offset] = rex;
		buffer[offset + 1] = 0x8D;
		buffer[offset + 2] = modrm;
		const auto displacement = static_cast<std::int32_t>(static_cast<std::int64_t>(k_target) - static_cast<std::int64_t>(offset + 7));
		std::memcpy(&buffer[offset + 3], &displacement, sizeof(displacement));
	}
}

TEST_CASE("scan_references finds rip-relative leas at both ends of the range and skips look-alikes")
{
	std::array<std::uint8_t, k_size> buffer{};
	buffer.fill(0x90);
	put_lea(buffer, 0, 0x48, 0x05);          // lea rax, [rip + d] at the very first byte
	buffer[9] = 0x8D;                        // an opcode byte with no REX.W prefix before it
	put_lea(buffer, 20, 0x48, 0x04);         // [rsp] addressing, not rip-relative
	put_lea(buffer, k_size - 7, 0x4C, 0x0D); // lea r9, [rip + d] ending on the last byte

	const rml::memory::range range(rml::memory::handle(buffer.data()), buffer.size());
	const auto target = rml::memory::handle(buffer.data() + k_target);

	const auto all = range.scan_references(target);
	REQUIRE(all.size() == 2);
	CHECK(all[0] == rml::memory::handle(buffer.data()));
	CHECK(all[1] == rml::memory::handle(buffer.data() + k_size - 7));

	const auto first = range.scan_references(target, 1);
	REQUIRE(first.size() == 1);
	CHECK(first[0] == rml::memory::handle(buffer.data()));
}

#endif
