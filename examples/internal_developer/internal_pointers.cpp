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

#if defined(RML_WINDOWS)
	namespace
	{
		// Studio builds its window title from this literal, right after asking whether the
		// session is internal. The literal survives compiler churn that moves every byte
		// around it, so it anchors the search instead of a signature.
		constexpr std::string_view internal_marker = "[Internal]";

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

		/// True when `function` looks like `bool IsInternal()`: a chain of global byte tests.
		bool looks_like_is_internal(const rml::memory::handle function)
		{
			const auto* const bytes = function.as<const std::uint8_t*>();
			return bytes[0] == cmp_opcode && bytes[1] == cmp_modrm;
		}

		/// Walks the function body and collects every global byte it tests.
		std::size_t collect_flags(const rml::memory::handle function)
		{
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

			return g_engine_pointers.flag_count;
		}

		/// The call that precedes the instruction loading `internal_marker`.
		std::optional<rml::memory::handle> find_is_internal(const rml::memory::range& studio, const rml::memory::handle reference)
		{
			for (std::size_t back = call_length; back <= call_backtrack; ++back)
			{
				const auto site = reference.sub(back);
				if (!studio.contains(site))
					continue;

				if (site.as<const std::uint8_t*>()[0] != call_opcode)
					continue;

				const auto target = site.add(1).rip();
				if (!studio.contains(target) || !looks_like_is_internal(target))
					continue;

				return target;
			}

			return std::nullopt;
		}

		bool resolve_windows(const rml::memory::module& studio)
		{
			const auto markers = studio.scan_strings(internal_marker, 2);
			if (markers.size() != 1)
			{
				RML_ERROR("Expected one '{}' literal, found {}", internal_marker, markers.size());
				return false;
			}

			const auto references = studio.scan_rip_references(markers.front(), 2);
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

			RML_INFO("Found 'IS_INTERNAL' RobloxStudioBeta.exe+0x{:X} with {} flag(s)",
			    is_internal->as<std::uintptr_t>() - studio.begin().as<std::uintptr_t>(),
			    g_engine_pointers.flag_count);

			return true;
		}
	}
#else
	static constexpr auto engine_batch()
	{
		// clang-format off
		return rml::memory::make_batch<
			{
			    "IS_INTERNAL",
			    "F4 03 00 AA ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? 9F 02 00 71 0A 5C 80 39 0B 30 40 A9 28 11 88 9A 5F 01 00 F1 69 B1 80 9A",
			    [](const rml::memory::handle ptr) {
				    const auto is_internal = ptr.sub(4).bl();

				    g_engine_pointers.add_flag(is_internal.adrp().as<bool*>());
				    g_engine_pointers.add_flag(is_internal.add(8).adrp().as<bool*>());

				    if (g_engine_pointers.flag_count == 0)
					    return;

				    g_engine_pointers.is_internal = is_internal.as<void*>();
			    },
			}
		>();
		// clang-format on
	}

	static bool resolve_macos(const rml::memory::module& studio)
	{
		const auto [batch, hash] = engine_batch();

		if (!rml::memory::batch_runner::run(batch, studio))
		{
			RML_ERROR("Signature scan failed; Roblox Studio was probably updated");
			return false;
		}

		return true;
	}
#endif

	bool resolve_engine_pointers()
	{
		const rml::memory::module studio{rml::platform::studio_image_name()};

#if defined(RML_WINDOWS)
		if (!resolve_windows(studio))
			return false;
#else
		if (!resolve_macos(studio))
			return false;
#endif

		if (!g_engine_pointers.complete())
		{
			RML_ERROR("IsInternal was located but the engine flags were not resolved");
			return false;
		}

		return true;
	}
}
