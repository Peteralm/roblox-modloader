#include "class_factory_probe.hpp"

#include <cstdint>

namespace rml::native
{
	namespace
	{
		const void* slot_value(const void* descriptor, const std::size_t offset, const ReadableFn readable) noexcept
		{
			const auto* address = static_cast<const std::byte*>(descriptor) + offset;
			if (!readable(address, sizeof(void*)))
				return nullptr;
			return *reinterpret_cast<const void* const*>(address);
		}

		bool offset_fits(const std::span<const void* const> creatable, const std::span<const void* const> non_creatable,
		    const std::size_t offset, const ReadableFn readable, const ExecutableFn executable,
		    const void*& refusal) noexcept
		{
			// Every creatable class must hold real code here, and at least two of
			// them must differ: a slot shared by all classes is not a factory.
			const void* first = nullptr;
			bool distinct = false;
			for (const auto* descriptor : creatable)
			{
				const auto* value = slot_value(descriptor, offset, readable);
				if (!value || !executable(value))
					return false;
				if (!first)
					first = value;
				else if (value != first)
					distinct = true;
			}
			if (!first || !distinct)
				return false;

			// Every non-creatable class must agree on one refusal value, and that
			// value must not be any factory.
			refusal = nullptr;
			for (const auto* descriptor : non_creatable)
			{
				const auto* address = static_cast<const std::byte*>(descriptor) + offset;
				if (!readable(address, sizeof(void*)))
					return false;
				const auto* value = *reinterpret_cast<const void* const*>(address);
				if (refusal && value != refusal)
					return false;
				refusal = value;
				for (const auto* other : creatable)
					if (slot_value(other, offset, readable) == value)
						return false;
			}
			return true;
		}
	}

	std::optional<FactorySlot> probe_factory_slot(const std::span<const void* const> creatable,
	    const std::span<const void* const> non_creatable, const std::size_t search_bytes, const ReadableFn readable,
	    const ExecutableFn executable) noexcept
	{
		if (creatable.size() < 2 || non_creatable.empty() || !readable || !executable
		    || search_bytes < sizeof(void*))
			return std::nullopt;
		for (const auto* descriptor : creatable)
			if (!descriptor)
				return std::nullopt;
		for (const auto* descriptor : non_creatable)
			if (!descriptor)
				return std::nullopt;

		std::optional<FactorySlot> found;
		for (std::size_t offset = 0; offset + sizeof(void*) <= search_bytes; offset += sizeof(void*))
		{
			const void* refusal = nullptr;
			if (!offset_fits(creatable, non_creatable, offset, readable, executable, refusal))
				continue;
			if (found)
				return std::nullopt; // ambiguous: refuse rather than guess
			found = FactorySlot{offset, refusal};
		}
		return found;
	}

	bool install_factory(void* descriptor, const FactorySlot& slot, const void* factory, const ReadableFn readable,
	    const ExecutableFn executable) noexcept
	{
		if (!descriptor || !factory || !readable || !executable || !executable(factory))
			return false;
		auto* address = static_cast<std::byte*>(descriptor) + slot.offset;
		if (!readable(address, sizeof(void*)))
			return false;
		// A descriptor cloned from a creatable class already carries that class's
		// factory here; a wrong offset would carry neither that nor the refusal
		// value, so both shapes are accepted and anything else is refused.
		const auto* current = *reinterpret_cast<const void* const*>(address);
		if (current != slot.non_creatable_value && !executable(current))
			return false;
		if (current == factory)
			return false;
		*reinterpret_cast<const void**>(address) = factory;
		return true;
	}
} // namespace rml::native
