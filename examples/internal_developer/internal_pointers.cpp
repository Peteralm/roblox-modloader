#include "internal_pointers.hpp"

#include "RobloxModLoader/internal/platform.hpp"
#include "RobloxModLoader/logger/logger.hpp"
#include "RobloxModLoader/platform/memory/host_image.hpp"
#include "RobloxModLoader/util/compile_time_helpers.hpp"

#include <algorithm>
#include <vector>

#if defined(RML_WINDOWS)
	#include <windows.h>
#endif

RML_LOG_SCOPE("InternalDeveloper")

namespace internal_developer
{
	static EnginePointers g_engine_pointers{};

	EnginePointers& engine_pointers() noexcept
	{
		return g_engine_pointers;
	}

	namespace
	{
		// Studio builds its window title from this literal, right after asking whether the
		// session is internal. The literal comes from the source, so it survives the compiler
		// churn that moves every byte around it: it anchors the search instead of a signature.
		constexpr std::string_view internal_marker = "[Internal]";

		// The decoders below are picked by architecture, exactly like rml::memory::range does:
		// a macOS x86_64 host runs the x86-64 path, not the arm64 one.
#if defined(__x86_64__) || defined(_M_X64)
		// cmp byte ptr [rip + disp32], 0 -> 80 3D <disp32> 00
		constexpr std::uint8_t cmp_opcode = 0x80;
		constexpr std::uint8_t cmp_modrm = 0x3D;
		constexpr std::uint8_t cmp_immediate = 0x00;
		constexpr std::size_t cmp_length = 7;
		constexpr std::size_t cmp_displacement = 2;

		constexpr std::uint8_t call_opcode = 0xE8;
		constexpr std::size_t call_length = 5;
		constexpr std::size_t call_backtrack = 0x20;

		// A `cmp byte ptr [rip + d], 0` only exists to feed a branch or a setcc, so the byte
		// after the seven we matched tells us whether we landed on a real instruction boundary
		// or in the middle of somebody else's displacement.
		constexpr std::uint8_t two_byte_escape = 0x0F;
		constexpr std::uint8_t jcc_short_low = 0x70;
		constexpr std::uint8_t jcc_short_high = 0x7F;
		constexpr std::uint8_t jcc_near_low = 0x80;
		constexpr std::uint8_t setcc_high = 0x9F;

		// Anything that ends the function body: int3 padding, ret, ret imm16, jmp rel32.
		constexpr std::uint8_t padding = 0xCC;
		constexpr std::uint8_t ret_near = 0xC3;
		constexpr std::uint8_t ret_immediate = 0xC2;
		constexpr std::uint8_t jmp_relative = 0xE9;

		constexpr std::size_t body_length = 0x40;
#elif defined(__aarch64__) || defined(_M_ARM64)
		// adrp xN, <page> / bl <offset>
		constexpr std::uint32_t adrp_mask = 0x9F000000;
		constexpr std::uint32_t adrp_value = 0x90000000;
		constexpr std::uint32_t bl_mask = 0xFC000000;
		constexpr std::uint32_t bl_value = 0x94000000;
		constexpr std::uint32_t ret_instruction = 0xD65F03C0;

		// The instruction that consumes the adrp: ldrb/strb (unsigned offset) or add immediate,
		// and its Rn has to be the register the adrp wrote.
		constexpr std::uint32_t byte_access_mask = 0xFFC00000;
		constexpr std::uint32_t strb_value = 0x39000000;
		constexpr std::uint32_t ldrb_value = 0x39400000;
		constexpr std::uint32_t add_immediate_mask = 0xFF800000;
		constexpr std::uint32_t add_immediate_value = 0x91000000;
		constexpr std::uint32_t register_mask = 0x1F;
		constexpr std::uint32_t source_register_shift = 5;

		constexpr std::size_t instruction_length = 4;
		constexpr std::size_t adrp_pair_length = 8;
		constexpr std::size_t call_backtrack = 0x20;
		constexpr std::size_t body_length = 0x40;
#endif

		/// Bytes readable from `address` without leaving the image, capped at `wanted`.
		std::size_t readable_span(const rml::memory::range& studio, const rml::memory::handle address,
		    const std::size_t wanted)
		{
			if (!studio.contains(address))
				return 0;

			const auto available = studio.end().as<std::uintptr_t>() - address.as<std::uintptr_t>();
			return std::min<std::size_t>(wanted, static_cast<std::size_t>(available));
		}

		/// True when `flag` sits in committed, writable, non-executable memory. The hook writes
		/// through this pointer, so a stray decode must never reach it.
		bool is_writable_data(const rml::memory::handle flag)
		{
#if defined(RML_WINDOWS)
			MEMORY_BASIC_INFORMATION information{};
			if (VirtualQuery(flag.as<const void*>(), &information, sizeof(information)) == 0)
				return false;

			if (information.State != MEM_COMMIT)
				return false;

			// .data / .bss only: executable pages are code, read-only pages are .rdata.
			return (information.Protect & (PAGE_READWRITE | PAGE_WRITECOPY)) != 0;
#else
			// No cheap per-page query here; containment in the image is the only guarantee.
			(void) flag;
			return true;
#endif
		}

		/// Records `flag` if it really is a writable global inside Studio, and says why if not.
		void accept_flag(const rml::memory::range& studio, const rml::memory::handle flag, const std::size_t offset)
		{
			if (!studio.contains(flag))
			{
				RML_WARN("Rejecting flag from +0x{:X}: 0x{:X} lies outside the Studio image", offset,
				    flag.as<std::uintptr_t>());
				return;
			}

			if (!is_writable_data(flag))
			{
				RML_WARN("Rejecting flag from +0x{:X}: 0x{:X} is not writable data", offset,
				    flag.as<std::uintptr_t>());
				return;
			}

			g_engine_pointers.add_flag(flag.as<bool*>());
		}

#if defined(__x86_64__) || defined(_M_X64)
		/// True when `bytes` starts the conditional branch or setcc that a flag test feeds.
		bool consumes_flags(const std::uint8_t* const bytes, const std::size_t available)
		{
			if (available == 0)
				return false;

			if (bytes[0] >= jcc_short_low && bytes[0] <= jcc_short_high)
				return true;

			if (bytes[0] != two_byte_escape || available < 2)
				return false;

			return bytes[1] >= jcc_near_low && bytes[1] <= setcc_high;
		}

		/// True when a whole `cmp byte ptr [rip + disp32], 0` starts at `bytes`, branch included.
		bool is_flag_test(const std::uint8_t* const bytes, const std::size_t available)
		{
			if (available < cmp_length)
				return false;

			if (bytes[0] != cmp_opcode || bytes[1] != cmp_modrm || bytes[cmp_length - 1] != cmp_immediate)
				return false;

			return consumes_flags(bytes + cmp_length, available - cmp_length);
		}

		/// True when `byte` ends a function body.
		bool ends_body(const std::uint8_t byte)
		{
			return byte == padding || byte == ret_near || byte == ret_immediate || byte == jmp_relative;
		}
#elif defined(__aarch64__) || defined(_M_ARM64)
		/// True when `paired` reads or offsets the byte `adrp` addressed, in the same register.
		bool completes_adrp(const std::uint32_t adrp, const std::uint32_t paired)
		{
			if (((paired >> source_register_shift) & register_mask) != (adrp & register_mask))
				return false;

			if ((paired & byte_access_mask) == ldrb_value || (paired & byte_access_mask) == strb_value)
				return true;

			return (paired & add_immediate_mask) == add_immediate_value;
		}
#endif

		/// True when `function` starts by reading a global byte, which is all `IsInternal` does.
		bool looks_like_is_internal(const rml::memory::range& studio, const rml::memory::handle function)
		{
#if defined(__x86_64__) || defined(_M_X64)
			const std::size_t available = readable_span(studio, function, cmp_length + 2);
			return is_flag_test(function.as<const std::uint8_t*>(), available);
#elif defined(__aarch64__) || defined(_M_ARM64)
			if (readable_span(studio, function, adrp_pair_length) < adrp_pair_length)
				return false;

			const auto* const instructions = function.as<const std::uint32_t*>();
			if ((instructions[0] & adrp_mask) != adrp_value)
				return false;

			return completes_adrp(instructions[0], instructions[1]);
#else
			(void) studio;
			(void) function;
			return false;
#endif
		}

		/// Walks the function body and collects every global byte it tests.
		std::size_t collect_flags(const rml::memory::range& studio, const rml::memory::handle function)
		{
#if defined(__x86_64__) || defined(_M_X64)
			const std::size_t span = readable_span(studio, function, body_length);
			const auto* const bytes = function.as<const std::uint8_t*>();

			for (std::size_t offset = 0; offset + cmp_length <= span;)
			{
				if (ends_body(bytes[offset]))
					break;

				if (is_flag_test(bytes + offset, span - offset))
				{
					// The displacement is relative to the end of the instruction, immediate included.
					accept_flag(studio, function.add(offset + cmp_displacement).rip().add(1), offset);
					offset += cmp_length;
					continue;
				}

				++offset;
			}
#elif defined(__aarch64__) || defined(_M_ARM64)
			const std::size_t span = readable_span(studio, function, body_length);

			for (std::size_t offset = 0; offset + adrp_pair_length <= span; offset += instruction_length)
			{
				const auto instruction = function.add(offset);
				const auto* const instructions = instruction.as<const std::uint32_t*>();

				if (instructions[0] == ret_instruction)
					break;

				if ((instructions[0] & adrp_mask) != adrp_value)
					continue;

				// Only an adrp whose pair reads or offsets the same register addresses a flag;
				// the adrps that build strings or vtables pair with something else.
				if (!completes_adrp(instructions[0], instructions[1]))
					continue;

				accept_flag(studio, instruction.adrp(), offset);
			}
#else
			(void) studio;
			(void) function;
#endif

			return g_engine_pointers.flag_count;
		}

		/// The call that precedes the instruction loading `internal_marker`.
		std::optional<rml::memory::handle> find_is_internal(const rml::memory::range& studio,
		    const rml::memory::handle reference)
		{
			std::vector<rml::memory::handle> candidates;

#if defined(__x86_64__) || defined(_M_X64)
			for (std::size_t back = call_length; back <= call_backtrack; ++back)
			{
				const auto site = reference.sub(back);
				if (readable_span(studio, site, call_length) < call_length)
					continue;

				const auto opcode = site.as<const std::uint8_t*>()[0];

				// Padding means the walk left this function; anything before it is somebody else's.
				if (opcode == padding)
					break;

				if (opcode != call_opcode)
					continue;

				const auto target = site.add(1).rip();
				if (!looks_like_is_internal(studio, target))
					continue;

				if (std::find(candidates.begin(), candidates.end(), target) == candidates.end())
					candidates.push_back(target);
			}
#elif defined(__aarch64__) || defined(_M_ARM64)
			for (std::size_t back = instruction_length; back <= call_backtrack; back += instruction_length)
			{
				const auto site = reference.sub(back);
				if (readable_span(studio, site, instruction_length) < instruction_length)
					continue;

				const auto encoded = site.as<const std::uint32_t&>();
				if (encoded == ret_instruction)
					break;

				if ((encoded & bl_mask) != bl_value)
					continue;

				const auto target = site.bl();
				if (!looks_like_is_internal(studio, target))
					continue;

				if (std::find(candidates.begin(), candidates.end(), target) == candidates.end())
					candidates.push_back(target);
			}
#else
			(void) studio;
			(void) reference;
#endif

			if (candidates.size() != 1)
			{
				RML_ERROR("Expected one IsInternal call before the '{}' reference, found {}", internal_marker,
				    candidates.size());
				return std::nullopt;
			}

			return candidates.front();
		}
	}

	bool resolve_engine_pointers()
	{
		const rml::memory::module studio{rml::platform::studio_image_name()};

		const auto markers = studio.scan_strings(internal_marker, 2);
		if (markers.size() != 1)
		{
			RML_ERROR("Expected one '{}' literal, found {}", internal_marker, markers.size());
			return false;
		}

		const auto references = studio.scan_references(markers.front(), 2);
		if (references.size() != 1)
		{
			RML_ERROR("Expected one reference to '{}', found {}", internal_marker, references.size());
			return false;
		}

		const auto is_internal = find_is_internal(studio, references.front());
		if (!is_internal.has_value())
			return false;

		if (collect_flags(studio, *is_internal) == 0)
		{
			RML_ERROR("IsInternal tests no usable global flag");
			return false;
		}

		g_engine_pointers.is_internal = is_internal->as<void*>();

		RML_INFO("Found 'IS_INTERNAL' at +0x{:X} with {} flag(s)",
		    is_internal->as<std::uintptr_t>() - studio.begin().as<std::uintptr_t>(),
		    g_engine_pointers.flag_count);

		return g_engine_pointers.complete();
	}
}
