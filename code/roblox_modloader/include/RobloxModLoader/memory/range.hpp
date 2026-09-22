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
		/// Matches are whole literals only: the match must start the image or follow a NUL,
		/// and must be followed by one, so "[Internal]" never matches "Foo[Internal]Bar".
		[[nodiscard]] std::vector<handle> scan_strings(std::string_view text, std::size_t limit = 0) const;

		/// Instructions that materialize `target`: `lea r64, [rip + disp32]` on x86-64,
		/// `adrp` plus the immediately following `add`/`ldr` on the same register on arm64.
		/// Returns the address of the instruction that starts each reference. `limit` of 0
		/// collects every match.
		///
		/// Scope, deliberately narrow so that a match is unambiguous: on x86-64 only the
		/// REX.W form (`48`/`4C` `8D`) is decoded — a non-REX `8D 05`, a `mov r64, [rip + d]`
		/// load and a pointer sitting in a `.rdata` table are all invisible. On arm64 only the
		/// adjacent `adrp`/`add`-`ldr` pair is decoded. "Exactly one match" therefore means
		/// "exactly one reference of the decoded form", which is the form compilers emit for a
		/// string literal address; treat a count of 0 as "anchor not found", never as proof
		/// that nothing else in the image mentions `target`.
		[[nodiscard]] std::vector<handle> scan_references(handle target, std::size_t limit = 0) const;

	protected:
		handle m_base;
		std::size_t m_size;
	};
}
