#include "RobloxModLoader/memory/string_anchor.hpp"

#include "RobloxModLoader/internal/common.hpp"
#include "RobloxModLoader/memory/module.hpp"
#include "RobloxModLoader/platform/memory/host_image.hpp"

#include <algorithm>
#include <mach-o/loader.h>
#include <optional>
#include <string>

namespace rml::memory
{
	struct ImageLayout
	{
		std::uintptr_t base{};
		std::uintptr_t text_begin{};
		std::uintptr_t text_end{};
		std::uintptr_t image_end{};
		std::vector<std::uintptr_t> function_starts;
	};

	static ImageLayout read_image_layout()
	{
		ImageLayout layout;
		const module image(platform::studio_image_name());
		layout.base = image.begin().as<std::uintptr_t>();
		layout.image_end = layout.base + image.size();

		const auto* header = reinterpret_cast<const mach_header_64*>(layout.base);
		const auto* command = reinterpret_cast<const load_command*>(header + 1);
		std::uintptr_t slide = 0;
		std::uintptr_t linkedit_vmaddr = 0;
		std::uintptr_t linkedit_fileoff = 0;
		std::uintptr_t text_vmaddr = 0;
		std::uint32_t starts_offset = 0;
		std::uint32_t starts_size = 0;

		for (std::uint32_t i = 0; i < header->ncmds; ++i)
		{
			if (command->cmd == LC_SEGMENT_64)
			{
				const auto* segment = reinterpret_cast<const segment_command_64*>(command);
				const std::string_view name(segment->segname);
				if (name == "__TEXT")
				{
					text_vmaddr = segment->vmaddr;
					slide = layout.base - segment->vmaddr;
					const auto* section = reinterpret_cast<const section_64*>(segment + 1);
					for (std::uint32_t s = 0; s < segment->nsects; ++s, ++section)
					{
						if (std::string_view(section->sectname) == "__text")
						{
							layout.text_begin = section->addr + slide;
							layout.text_end = layout.text_begin + section->size;
						}
					}
				}
				else if (name == "__LINKEDIT")
				{
					linkedit_vmaddr = segment->vmaddr;
					linkedit_fileoff = segment->fileoff;
				}
			}
			else if (command->cmd == LC_FUNCTION_STARTS)
			{
				const auto* entry = reinterpret_cast<const linkedit_data_command*>(command);
				starts_offset = entry->dataoff;
				starts_size = entry->datasize;
			}
			command = reinterpret_cast<const load_command*>(reinterpret_cast<const std::byte*>(command) + command->cmdsize);
		}

		if (starts_size && linkedit_vmaddr)
		{
			const auto* cursor = reinterpret_cast<const std::uint8_t*>(linkedit_vmaddr + slide + (starts_offset - linkedit_fileoff));
			const auto* end = cursor + starts_size;
			std::uintptr_t address = text_vmaddr + slide;
			while (cursor < end)
			{
				std::uint64_t delta = 0;
				unsigned shift = 0;
				std::uint8_t byte;
				do
				{
					byte = *cursor++;
					delta |= static_cast<std::uint64_t>(byte & 0x7F) << shift;
					shift += 7;
				} while ((byte & 0x80) && cursor < end);
				if (delta == 0 && !layout.function_starts.empty())
					break;
				address += delta;
				layout.function_starts.push_back(address);
			}
			std::sort(layout.function_starts.begin(), layout.function_starts.end());
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

	static std::int64_t adrp_page(const std::uintptr_t pc, const std::uint32_t instruction)
	{
		const std::int64_t immlo = (instruction >> 29) & 0x3;
		const std::int64_t immhi = (instruction >> 5) & 0x7FFFF;
		std::int64_t imm = ((immhi << 2) | immlo) << 12;
		if (imm & (std::int64_t{1} << 32))
			imm -= std::int64_t{1} << 33;
		return static_cast<std::int64_t>(pc & ~std::uintptr_t{0xFFF}) + imm;
	}

	static std::vector<std::uintptr_t> code_references(const ImageLayout& layout, const std::vector<std::uintptr_t>& targets)
	{
		std::vector<std::uintptr_t> references;
		const auto* code = reinterpret_cast<const std::uint32_t*>(layout.text_begin);
		const std::size_t count = (layout.text_end - layout.text_begin) / 4;

		for (std::size_t i = 0; i + 1 < count; ++i)
		{
			const auto instruction = code[i];
			if ((instruction & 0x9F000000) != 0x90000000)
				continue;

			const auto pc = layout.text_begin + i * 4;
			const auto page = adrp_page(pc, instruction);
			const auto next = code[i + 1];
			const auto rd = instruction & 0x1F;
			const auto rn = (next >> 5) & 0x1F;
			std::int64_t resolved = -1;

			if ((next & 0xFF800000) == 0x91000000 && rn == rd)
				resolved = page + ((next >> 10) & 0xFFF);
			else if ((next & 0x3B000000) == 0x39000000 && rn == rd)
				resolved = page + (static_cast<std::int64_t>((next >> 10) & 0xFFF) << (next >> 30));

			if (resolved < 0)
				continue;

			for (const auto target : targets)
			{
				if (static_cast<std::uintptr_t>(resolved) == target)
					references.push_back(pc);
			}
		}

		return references;
	}

	static std::optional<AnchoredFunction> containing_function(const ImageLayout& layout, const std::uintptr_t address)
	{
		if (address < layout.text_begin || address >= layout.text_end)
			return std::nullopt;

		const auto it = std::upper_bound(layout.function_starts.begin(), layout.function_starts.end(), address);
		if (it == layout.function_starts.begin())
			return std::nullopt;

		const auto start = *(it - 1);
		const auto end = it == layout.function_starts.end() ? layout.text_end : *it;
		return AnchoredFunction{reinterpret_cast<void*>(start), end - start};
	}

	static std::vector<AnchoredFunction> unique_functions(const ImageLayout& layout, const std::vector<std::uintptr_t>& addresses)
	{
		std::vector<AnchoredFunction> result;
		for (const auto address : addresses)
		{
			const auto function = containing_function(layout, address);
			if (!function)
				continue;
			if (std::none_of(result.begin(), result.end(), [&](const AnchoredFunction& f) {
				    return f.start == function->start;
			    }))
				result.push_back(*function);
		}
		return result;
	}

	std::vector<AnchoredFunction> functions_referencing_string(const std::string_view exact_text)
	{
		const auto& layout = image_layout();
		if (layout.function_starts.empty() || !layout.text_begin)
			return {};

		const auto strings = exact_string_addresses(layout, exact_text);
		if (strings.empty())
			return {};

		return unique_functions(layout, code_references(layout, strings));
	}

	std::vector<AnchoredFunction> functions_calling(const void* target)
	{
		const auto& layout = image_layout();
		if (layout.function_starts.empty() || !layout.text_begin)
			return {};

		const auto wanted = reinterpret_cast<std::uintptr_t>(target);
		const auto* code = reinterpret_cast<const std::uint32_t*>(layout.text_begin);
		const std::size_t count = (layout.text_end - layout.text_begin) / 4;

		std::vector<std::uintptr_t> sites;
		for (std::size_t i = 0; i < count; ++i)
		{
			const auto instruction = code[i];
			if ((instruction & 0x7C000000) != 0x14000000)
				continue;

			std::int64_t imm = instruction & 0x03FFFFFF;
			if (imm & 0x02000000)
				imm -= 0x04000000;

			const auto pc = layout.text_begin + i * 4;
			if (pc + imm * 4 == wanted)
				sites.push_back(pc);
		}

		return unique_functions(layout, sites);
	}

	std::optional<AnchoredFunction> function_containing(const void* address)
	{
		const auto& layout = image_layout();
		if (layout.function_starts.empty() || !layout.text_begin)
			return std::nullopt;
		return containing_function(layout, reinterpret_cast<std::uintptr_t>(address));
	}
}
