#include "RobloxModLoader/memory/string_anchor.hpp"

#include "RobloxModLoader/internal/common.hpp"
#include "RobloxModLoader/memory/module.hpp"
#include "RobloxModLoader/platform/memory/host_image.hpp"

#include <algorithm>
#include <cstring>
#include <string>

namespace rml::memory
{
	struct ImageLayout
	{
		std::uintptr_t base{};
		std::uintptr_t text_begin{};
		std::uintptr_t text_end{};
		std::uintptr_t image_end{};
		const IMAGE_RUNTIME_FUNCTION_ENTRY* functions{};
		std::size_t function_count{};
	};

	static ImageLayout read_image_layout()
	{
		ImageLayout layout;
		const module image(platform::studio_image_name());
		layout.base = image.begin().as<std::uintptr_t>();

		const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(layout.base);
		const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(layout.base + dos->e_lfanew);
		layout.image_end = layout.base + nt->OptionalHeader.SizeOfImage;

		const auto* section = IMAGE_FIRST_SECTION(nt);
		for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section)
		{
			if (std::string_view(reinterpret_cast<const char*>(section->Name), 5) == ".text")
			{
				layout.text_begin = layout.base + section->VirtualAddress;
				layout.text_end = layout.text_begin + section->Misc.VirtualSize;
			}
		}

		const auto& exceptions = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
		if (exceptions.VirtualAddress)
		{
			layout.functions = reinterpret_cast<const IMAGE_RUNTIME_FUNCTION_ENTRY*>(layout.base + exceptions.VirtualAddress);
			layout.function_count = exceptions.Size / sizeof(IMAGE_RUNTIME_FUNCTION_ENTRY);
		}

		return layout;
	}

	static const ImageLayout& image_layout()
	{
		static const ImageLayout layout = read_image_layout();
		return layout;
	}

	static std::vector<std::uintptr_t> exact_string_addresses(const ImageLayout& layout, const std::string_view text)
	{
		const std::string_view haystack{reinterpret_cast<const char*>(layout.base), layout.image_end - layout.base};
		std::string needle;
		needle.push_back('\0');
		needle.append(text);
		needle.push_back('\0');

		std::vector<std::uintptr_t> found;
		for (auto position = haystack.find(needle); position != std::string_view::npos; position = haystack.find(needle, position + 1))
			found.push_back(layout.base + position + 1);
		return found;
	}

	static std::vector<std::uintptr_t> code_references(const ImageLayout& layout, const std::vector<std::uintptr_t>& targets)
	{
		std::vector<std::uintptr_t> references;
		const auto* code = reinterpret_cast<const std::uint8_t*>(layout.text_begin);
		const std::size_t count = layout.text_end - layout.text_begin;

		for (std::size_t i = 0; i + 7 <= count; ++i)
		{
			const auto rex = code[i];
			if ((rex & 0xF8) != 0x48 || code[i + 1] != 0x8D || (code[i + 2] & 0xC7) != 0x05)
				continue;

			std::int32_t displacement;
			std::memcpy(&displacement, code + i + 3, sizeof(displacement));
			const auto resolved = layout.text_begin + i + 7 + displacement;

			for (const auto target : targets)
			{
				if (resolved == target)
					references.push_back(layout.text_begin + i);
			}
		}

		return references;
	}

	std::vector<AnchoredFunction> functions_referencing_string(const std::string_view exact_text)
	{
		const auto& layout = image_layout();
		if (!layout.functions || !layout.text_begin)
			return {};

		const auto strings = exact_string_addresses(layout, exact_text);
		if (strings.empty())
			return {};

		std::vector<AnchoredFunction> result;
		for (const auto reference : code_references(layout, strings))
		{
			const auto rva = static_cast<std::uint32_t>(reference - layout.base);
			const auto* entry = std::upper_bound(layout.functions, layout.functions + layout.function_count, rva,
			    [](const std::uint32_t value, const IMAGE_RUNTIME_FUNCTION_ENTRY& e) { return value < e.BeginAddress; });
			if (entry == layout.functions)
				continue;
			--entry;
			if (rva >= entry->EndAddress)
				continue;

			AnchoredFunction function{reinterpret_cast<void*>(layout.base + entry->BeginAddress), entry->EndAddress - entry->BeginAddress};
			if (std::none_of(result.begin(), result.end(), [&](const AnchoredFunction& f) { return f.start == function.start; }))
				result.push_back(function);
		}

		return result;
	}
}
