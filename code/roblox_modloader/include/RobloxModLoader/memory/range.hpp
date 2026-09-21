#pragma once
#include "fwddec.hpp"
#include "handle.hpp"

#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

namespace rml::memory
{
	class RML_EXPORT range
	{
	public:
		range(handle base, std::size_t size);

		handle begin() const;

		handle end() const;

		std::size_t size() const;

		bool contains(handle h) const;

		std::optional<handle> scan(pattern const& sig) const;

		/// Occurrences of a NUL-terminated byte string. `limit` of 0 collects every match.
		/// A string literal outlives the instructions around it, so it is the most durable
		/// anchor available: scan for it, then find the code that loads it.
		[[nodiscard]] std::vector<handle> scan_strings(std::string_view text, std::size_t limit = 0) const;

		/// Instructions that materialize `target`: `lea r64, [rip + disp32]` on x86-64,
		/// `adrp` (with the add/ldr that follows it) on arm64. Returns the address of the
		/// instruction that starts each reference. `limit` of 0 collects every match.
		[[nodiscard]] std::vector<handle> scan_references(handle target, std::size_t limit = 0) const;

	protected:
		handle m_base;
		std::size_t m_size;
	};
}
