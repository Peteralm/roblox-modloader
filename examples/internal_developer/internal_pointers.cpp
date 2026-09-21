#include "internal_pointers.hpp"

#include "RobloxModLoader/internal/platform.hpp"
#include "RobloxModLoader/logger/logger.hpp"
#include "RobloxModLoader/platform/memory/host_image.hpp"
#include "RobloxModLoader/util/compile_time_helpers.hpp"

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

#if defined(RML_WINDOWS)
		// cmp byte ptr [rip + disp32], 0 -> 80 3D <disp32> 00
		constexpr std::uint8_t cmp_opcode = 0x80;
		constexpr std::uint8_t cmp_modrm = 0x3D;
		constexpr std::uint8_t cmp_immediate = 0x00;
		constexpr std::size_t cmp_length = 7;
		constexpr std::size_t cmp_displacement = 2;

		constexpr std::uint8_t call_opcode = 0xE8;
		constexpr std::size_t call_length = 5;
		constexpr std::size_t call_backtrack = 0x20;

		constexpr std::uint8_t padding = 0xCC;
		constexpr std::size_t body_length = 0x40;
#else
		// adrp xN, <page> / bl <offset>
		constexpr std::uint32_t adrp_mask = 0x9F000000;
		constexpr std::uint32_t adrp_value = 0x90000000;
		constexpr std::uint32_t bl_mask = 0xFC000000;
		constexpr std::uint32_t bl_value = 0x94000000;
		constexpr std::uint32_t ret_instruction = 0xD65F03C0;
		constexpr std::size_t instruction_length = 4;
		constexpr std::size_t call_backtrack = 0x20;
		constexpr std::size_t body_length = 0x40;
#endif

		/// True when `function` starts by reading a global byte, which is all `IsInternal` does.
		bool looks_like_is_internal(const rml::memory::handle function)
		{
#if defined(RML_WINDOWS)
			const auto* const bytes = function.as<const std::uint8_t*>();
			return bytes[0] == cmp_opcode && bytes[1] == cmp_modrm;
#else
			return (function.as<const std::uint32_t&>() & adrp_mask) == adrp_value;
#endif
		}

		/// Walks the function body and collects every global byte it tests.
		std::size_t collect_flags(const rml::memory::handle function)
		{
#if defined(RML_WINDOWS)
			const auto* const bytes = function.as<const std::uint8_t*>();

			for (std::size_t offset = 0; offset + cmp_length <= body_length;)
			{
				if (bytes[offset] == padding)
					break;

				if (bytes[offset] == cmp_opcode && bytes[offset + 1] == cmp_modrm &&
				    bytes[offset + cmp_length - 1] == cmp_immediate)
				{
					g_engine_pointers.add_flag(function.add(offset + cmp_displacement).rip().add(1).as<bool*>());
					offset += cmp_length;
					continue;
				}

				++offset;
			}
#else
			for (std::size_t offset = 0; offset + instruction_length <= body_length; offset += instruction_length)
			{
				const auto instruction = function.add(offset);
				const auto encoded = instruction.as<const std::uint32_t&>();

				if (encoded == ret_instruction)
					break;

				if ((encoded & adrp_mask) != adrp_value)
					continue;

				g_engine_pointers.add_flag(instruction.adrp().as<bool*>());
			}
#endif

			return g_engine_pointers.flag_count;
		}

		/// The call that precedes the instruction loading `internal_marker`.
		std::optional<rml::memory::handle> find_is_internal(const rml::memory::range& studio,
		    const rml::memory::handle reference)
		{
#if defined(RML_WINDOWS)
			for (std::size_t back = call_length; back <= call_backtrack; ++back)
			{
				const auto site = reference.sub(back);
				if (!studio.contains(site) || site.as<const std::uint8_t*>()[0] != call_opcode)
					continue;

				const auto target = site.add(1).rip();
				if (studio.contains(target) && looks_like_is_internal(target))
					return target;
			}
#else
			for (std::size_t back = instruction_length; back <= call_backtrack; back += instruction_length)
			{
				const auto site = reference.sub(back);
				if (!studio.contains(site) || (site.as<const std::uint32_t&>() & bl_mask) != bl_value)
					continue;

				const auto target = site.bl();
				if (studio.contains(target) && looks_like_is_internal(target))
					return target;
			}
#endif

			return std::nullopt;
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
		{
			RML_ERROR("No IsInternal call precedes the '{}' reference", internal_marker);
			return false;
		}

		if (collect_flags(*is_internal) == 0)
		{
			RML_ERROR("IsInternal tests no global flag");
			return false;
		}

		g_engine_pointers.is_internal = is_internal->as<void*>();

		RML_INFO("Found 'IS_INTERNAL' at +0x{:X} with {} flag(s)",
		    is_internal->as<std::uintptr_t>() - studio.begin().as<std::uintptr_t>(),
		    g_engine_pointers.flag_count);

		return g_engine_pointers.complete();
	}
}
