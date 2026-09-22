#include "RobloxModLoader/memory/range.hpp"

#include "RobloxModLoader/internal/common.hpp"
#include "RobloxModLoader/memory/pattern.hpp"

#include <cstring>

namespace rml::memory
{
	namespace
	{
		// lea r64, [rip + disp32] -> REX.W + 8D /r with mod = 00 and r/m = 101.
		constexpr std::uint8_t rex_w = 0x48;
		constexpr std::uint8_t rex_wr = 0x4C;
		constexpr std::uint8_t lea_opcode = 0x8D;
		constexpr std::uint8_t modrm_mask = 0xC7;
		constexpr std::uint8_t modrm_rip_relative = 0x05;
		constexpr std::size_t lea_length = 7;
		constexpr std::size_t lea_displacement = 3;

		// adrp xN, <page> -> 1xx1 0000 with the immediate spread across the instruction.
		constexpr std::uint32_t adrp_mask = 0x9F000000;
		constexpr std::uint32_t adrp_value = 0x90000000;
		constexpr std::size_t instruction_length = 4;
		constexpr std::size_t adrp_pair_length = 8;

		// The instruction that applies the page offset: an unsigned-offset load/store or an
		// add immediate, reading the register the adrp wrote.
		constexpr std::uint32_t load_store_mask = 0x3B000000;
		constexpr std::uint32_t load_store_value = 0x39000000;
		constexpr std::uint32_t add_immediate_mask = 0xFF800000;
		constexpr std::uint32_t add_immediate_value = 0x91000000;
		constexpr std::uint32_t register_mask = 0x1F;
		constexpr std::uint32_t source_register_shift = 5;

		[[maybe_unused]] bool completes_adrp(const std::uint32_t adrp, const std::uint32_t paired)
		{
			if (((paired >> source_register_shift) & register_mask) != (adrp & register_mask))
				return false;

			return (paired & load_store_mask) == load_store_value ||
			       (paired & add_immediate_mask) == add_immediate_value;
		}
	}

	range::range(handle base, std::size_t size) :
	    m_base(base),
	    m_size(size)
	{
	}

	handle range::begin() const
	{
		return m_base;
	}

	handle range::end() const
	{
		return m_base.add(m_size);
	}

	std::size_t range::size() const
	{
		return m_size;
	}

	bool range::contains(handle h) const
	{
		return h.as<std::uintptr_t>() >= begin().as<std::uintptr_t>() && h.as<std::uintptr_t>() < end().as<std::uintptr_t>();
	}

	// https://en.wikipedia.org/wiki/Boyer%E2%80%93Moore%E2%80%93Horspool_algorithm
	// https://www.youtube.com/watch?v=AuZUeshhy-s
	std::optional<handle> scan_pattern(const std::optional<uint8_t>* sig, std::size_t length, handle begin, std::size_t module_size)
	{
		std::size_t maxShift = length;
		std::size_t max_idx = length - 1;

		//Get wildcard index, and store max shiftable byte count
		std::size_t wild_card_idx{static_cast<size_t>(-1)};
		for (int i{static_cast<int>(max_idx - 1)}; i >= 0; --i)
		{
			if (!sig[i])
			{
				maxShift = max_idx - i;
				wild_card_idx = i;
				break;
			}
		}

		//Store max shiftable bytes for non wildcards.
		std::size_t shift_table[UINT8_MAX + 1]{};
		for (std::size_t i{}; i <= UINT8_MAX; ++i)
		{
			shift_table[i] = maxShift;
		}

		//Fill shift table with sig bytes
		for (std::size_t i{wild_card_idx + 1}; i != max_idx; ++i)
		{
			shift_table[*sig[i]] = max_idx - i;
		}

		//Loop data
		const auto scan_end = module_size - length;
		for (std::size_t current_idx{}; current_idx <= scan_end;)
		{
			for (std::ptrdiff_t sig_idx{(std::ptrdiff_t)max_idx}; sig_idx >= 0; --sig_idx)
			{
				if (sig[sig_idx] && *begin.add(current_idx + sig_idx).as<uint8_t*>() != *sig[sig_idx])
				{
					current_idx += shift_table[*begin.add(current_idx + max_idx).as<uint8_t*>()];
					break;
				}
				else if (sig_idx == NULL)
				{
					return begin.add(current_idx);
				}
			}
		}
		return std::nullopt;
	}

	std::optional<handle> range::scan(pattern const& sig) const
	{
		auto data = sig.m_bytes.data();
		auto length = sig.m_bytes.size();

		if (auto result = scan_pattern(data, length, m_base, m_size); result)
		{
			return result;
		}

		return std::nullopt;
	}

	std::vector<handle> range::scan_strings(const std::string_view text, const std::size_t limit) const
	{
		std::vector<handle> results;

		if (text.empty())
			return results;

		// Both terminators are part of the needle: "[Internal]" must not match the tail of
		// "Foo[Internal]" nor the head of "[Internal]Foo".
		const std::size_t length = text.size() + 1;
		if (m_size < length)
			return results;

		const auto* const bytes = m_base.as<const char*>();
		const std::size_t scan_end = m_size - length;

		for (std::size_t offset = 0; offset <= scan_end; ++offset)
		{
			if (bytes[offset] != text.front())
				continue;

			if (offset != 0 && bytes[offset - 1] != '\0')
				continue;

			if (std::memcmp(bytes + offset, text.data(), text.size()) != 0)
				continue;

			if (bytes[offset + text.size()] != '\0')
				continue;

			results.push_back(m_base.add(offset));

			if (limit != 0 && results.size() >= limit)
				break;

			offset += text.size();
		}

		return results;
	}

	std::vector<handle> range::scan_references(const handle target, const std::size_t limit) const
	{
#if defined(__x86_64__) || defined(_M_X64)
		std::vector<handle> results;

		if (m_size < lea_length)
			return results;

		const auto* const bytes = m_base.as<const std::uint8_t*>();
		const auto wanted = target.as<std::uintptr_t>();
		const std::size_t scan_end = m_size - lea_length;

		for (std::size_t offset = 0; offset <= scan_end; ++offset)
		{
			if (bytes[offset] != rex_w && bytes[offset] != rex_wr)
				continue;

			if (bytes[offset + 1] != lea_opcode)
				continue;

			if ((bytes[offset + 2] & modrm_mask) != modrm_rip_relative)
				continue;

			const auto instruction = m_base.add(offset);
			if (instruction.add(lea_displacement).rip().as<std::uintptr_t>() != wanted)
				continue;

			results.push_back(instruction);

			if (limit != 0 && results.size() >= limit)
				break;

			offset += lea_length - 1;
		}

		return results;
#elif defined(__aarch64__) || defined(_M_ARM64)
		// adrp xN, page followed by the add/ldr that applies the page offset. Only the
		// immediately following instruction is decoded, and only when it reads the register
		// the adrp wrote: an adrp whose page happens to match is not a reference.
		std::vector<handle> results;

		if (m_size < adrp_pair_length)
			return results;

		const auto wanted = target.as<std::uintptr_t>();
		const std::size_t scan_end = m_size - adrp_pair_length;

		for (std::size_t offset = 0; offset <= scan_end; offset += instruction_length)
		{
			const auto instruction = m_base.add(offset);
			const auto* const instructions = instruction.as<const std::uint32_t*>();

			if ((instructions[0] & adrp_mask) != adrp_value)
				continue;

			if (!completes_adrp(instructions[0], instructions[1]))
				continue;

			if (instruction.adrp().as<std::uintptr_t>() != wanted)
				continue;

			results.push_back(instruction);

			if (limit != 0 && results.size() >= limit)
				break;
		}

		return results;
#else
		(void) target;
		(void) limit;
		return {};
#endif
	}
}
