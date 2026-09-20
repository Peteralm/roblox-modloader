#pragma once

#include <cstddef>
#include <optional>
#include <span>

namespace rml::native
{
	/// Answers whether a byte range can be read without faulting.
	using ReadableFn = bool (*)(const void*, std::size_t) noexcept;
	/// Answers whether an address lies in executable code.
	using ExecutableFn = bool (*)(const void*) noexcept;

	struct FactorySlot
	{
		/// Byte offset, inside a class descriptor, of the pointer the engine calls
		/// to build an instance of that class.
		std::size_t offset{};
		/// The value every non-creatable class carries there. A reserved class must
		/// never keep this value, or Instance.new would refuse it.
		const void* non_creatable_value{};
	};

	/// Derives the factory slot by contrast: creatable prototypes hold distinct
	/// executable pointers there, non-creatable ones all hold the same refusal
	/// value. Returns nothing when zero or several offsets fit, so a layout change
	/// disables the feature instead of corrupting a descriptor.
	[[nodiscard]] std::optional<FactorySlot> probe_factory_slot(std::span<const void* const> creatable,
	    std::span<const void* const> non_creatable, std::size_t search_bytes, ReadableFn readable,
	    ExecutableFn executable) noexcept;

	/// Publishes a factory into a descriptor the mod owns. The slot must currently
	/// hold either the refusal value or a cloned class's factory; anything else
	/// means the wrong offset and is refused.
	bool install_factory(void* descriptor, const FactorySlot& slot, const void* factory, ReadableFn readable,
	    ExecutableFn executable) noexcept;
} // namespace rml::native
