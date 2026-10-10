#include "RobloxModLoader/roblox/reflection/metadata/reflection_metadata.hpp"

#include "class_registry.hpp"

#include "RobloxModLoader/internal/common.hpp"
#include "RobloxModLoader/memory/module.hpp"
#include "RobloxModLoader/memory/string_anchor.hpp"
#include "RobloxModLoader/platform/memory/host_image.hpp"
#include "RobloxModLoader/platform/memory/memory_protection.hpp"
#include "RobloxModLoader/roblox/reflection/callback_descriptor.hpp"
#include "RobloxModLoader/roblox/reflection/event_descriptor.hpp"
#include "RobloxModLoader/roblox/reflection/function_descriptor.hpp"
#include "RobloxModLoader/roblox/reflection/property_descriptor.hpp"
#include "RobloxModLoader/roblox/reflection/yield_function_descriptor.hpp"
#include "pointers.hpp"

#include <atomic>
#include <utility>

namespace RBX::Reflection::Metadata
{
	template<typename Container>
	static const Member* member_of(const Reflection& reflection, const MemberDescriptor& descriptor)
	{
		const Class* c = reflection.get(descriptor.owner, false);
		const auto* members = c ? c->find_first_child_of_type<Container>() : nullptr;
		return members ? static_cast<const Member*>(members->find_first_child_by_name(descriptor.name.to_string())) : nullptr;
	}

	const Class* Class::get_base() const
	{
		return parent && parent->get_descriptor().is_a(class_name.data()) ? static_cast<const Class*>(parent) : nullptr;
	}

	const Class* Classes::get(const ClassDescriptor& descriptor, const bool find_best_match) const
	{
		if (const auto* c = static_cast<const Class*>(find_first_child_by_name(descriptor.name.to_string())))
			return c;
		if (!find_best_match || !descriptor.base)
			return nullptr;
		return get(*descriptor.base, true);
	}

	Class* Classes::get(const ClassDescriptor& descriptor, const bool find_best_match)
	{
		return const_cast<Class*>(std::as_const(*this).get(descriptor, find_best_match));
	}

	Reflection* Reflection::identify(void* candidate)
	{
		const auto* metadata_class = rml::reflection::ClassRegistry::instance().find_engine_class(class_name);
		if (!candidate || !metadata_class || reinterpret_cast<std::uintptr_t>(candidate) % alignof(void*) != 0 || !rml::platform::is_readable(candidate, sizeof(Instance)))
			return nullptr;
		auto* instance = static_cast<Reflection*>(candidate);
		return &instance->get_descriptor() == metadata_class ? instance : nullptr;
	}

	static Reflection* const* find_singleton_slot()
	{
		const auto getter = g_pointers ? reinterpret_cast<void*>(g_pointers->m_roblox_pointers.reflection_metadata_get_singleton) : nullptr;
		const auto function = getter ? rml::memory::function_containing(getter) : std::nullopt;
		if (!function)
			return nullptr;
		const rml::memory::module image(rml::platform::studio_image_name());
		for (auto* reference : rml::memory::data_references_from(*function))
		{
			if (!image.contains(rml::memory::handle(reference)) || reinterpret_cast<std::uintptr_t>(reference) % alignof(void*) != 0)
				continue;
			if (Reflection::identify(*static_cast<void* const*>(reference)))
				return static_cast<Reflection* const*>(reference);
		}
		return nullptr;
	}

	Reflection* Reflection::singleton()
	{
		static std::atomic<Reflection* const*> slot{nullptr};
		if (auto* resolved = slot.load(std::memory_order_acquire))
			return *resolved;
		auto* found = find_singleton_slot();
		if (!found)
			return nullptr;
		slot.store(found, std::memory_order_release);
		return *found;
	}

	const Class* Reflection::get(const ClassDescriptor& descriptor, const bool find_best_match) const
	{
		return classes ? std::as_const(*classes).get(descriptor, find_best_match) : nullptr;
	}

	Class* Reflection::get(const ClassDescriptor& descriptor, const bool find_best_match)
	{
		return const_cast<Class*>(std::as_const(*this).get(descriptor, find_best_match));
	}

	const Member* Reflection::get(const PropertyDescriptor& descriptor) const
	{
		return member_of<Properties>(*this, descriptor);
	}

	Member* Reflection::get(const PropertyDescriptor& descriptor)
	{
		return const_cast<Member*>(std::as_const(*this).get(descriptor));
	}

	const Member* Reflection::get(const FunctionDescriptor& descriptor) const
	{
		return member_of<Functions>(*this, descriptor);
	}

	Member* Reflection::get(const FunctionDescriptor& descriptor)
	{
		return const_cast<Member*>(std::as_const(*this).get(descriptor));
	}

	const Member* Reflection::get(const YieldFunctionDescriptor& descriptor) const
	{
		return member_of<YieldFunctions>(*this, descriptor);
	}

	Member* Reflection::get(const YieldFunctionDescriptor& descriptor)
	{
		return const_cast<Member*>(std::as_const(*this).get(descriptor));
	}

	const Member* Reflection::get(const EventDescriptor& descriptor) const
	{
		return member_of<Events>(*this, descriptor);
	}

	Member* Reflection::get(const EventDescriptor& descriptor)
	{
		return const_cast<Member*>(std::as_const(*this).get(descriptor));
	}

	const Member* Reflection::get(const CallbackDescriptor& descriptor) const
	{
		return member_of<Callbacks>(*this, descriptor);
	}

	Member* Reflection::get(const CallbackDescriptor& descriptor)
	{
		return const_cast<Member*>(std::as_const(*this).get(descriptor));
	}
}
