#include "RobloxModLoader/roblox/reflection/enum_descriptor.hpp"

#include <algorithm>

namespace RBX::Reflection
{
	const EnumDescriptor::Item* EnumDescriptor::find_item_by_value(const int value) const
	{
		if (!by_value)
			return static_cast<std::size_t>(static_cast<std::int64_t>(value)) < item_count ? &items[value] : nullptr;
		const std::span entries(by_value, by_value_count);
		const auto it = std::ranges::lower_bound(entries, value, {}, &ValueEntry::value);
		return it != entries.end() && it->value == value ? &items[it->item] : nullptr;
	}

	const EnumDescriptor::Item* EnumDescriptor::find_item_by_name(const std::string_view name) const
	{
		const auto hash = name_hash(name);
		const std::span entries(by_name, by_name_count);
		const auto it = std::ranges::lower_bound(entries, hash, {}, &NameEntry::hash);
		return it != entries.end() && it->hash == hash ? &items[it->item] : nullptr;
	}
}
