#include "RobloxModLoader/roblox/instance_factory.hpp"

#include "RobloxModLoader/internal/common.hpp"
#include "RobloxModLoader/logger/logger.hpp"
#include "RobloxModLoader/memory/foreign_call.hpp"
#include "RobloxModLoader/roblox/instance.hpp"
#include "RobloxModLoader/roblox/reflection/function_descriptor.hpp"
#include "RobloxModLoader/roblox/reflection/object.hpp"

#include <atomic>
#include <cstdint>

RML_LOG_SCOPE("InstanceFactory");

namespace rml::roblox
{
	namespace
	{
		/// The control block MSVC gives every `shared_ptr`: the strong count at
		/// +8, the weak count at +0xC, and the two virtuals the engine itself
		/// calls when either reaches zero.
		std::atomic<std::int32_t>* uses_of(void* control) noexcept
		{
			return reinterpret_cast<std::atomic<std::int32_t>*>(static_cast<std::byte*>(control) + 8);
		}

		std::atomic<std::int32_t>* weaks_of(void* control) noexcept
		{
			return reinterpret_cast<std::atomic<std::int32_t>*>(static_cast<std::byte*>(control) + 0xC);
		}

		/// `Destroy()` takes no arguments. The engine still asks the pack for its
		/// size, so this is all of it.
		class NoArguments final : public RBX::Reflection::FunctionDescriptor::Arguments
		{
		public:
			[[nodiscard]] std::size_t size() const override { return 0; }

			bool get_varint(int, RBX::Reflection::Variant&) const override { return false; }
			bool get_bool(int, bool&) const override { return false; }
			bool get_long(int, long&) const override { return false; }
			bool get_double(int, double&) const override { return false; }
			bool get_string(int, std::string&) const override { return false; }
			bool get_vector3_int16(int, RBX::Vector3int16&) const override { return false; }
			bool get_region3_int16(int, void*) const override { return false; }
			bool get_vector3(int, RBX::Vector3&) const override { return false; }
			bool get_region3(int, void*) const override { return false; }
			bool get_rect(int, RBX::Rect2D&) const override { return false; }
			bool get_object(int, std::shared_ptr<RBX::Reflection::DescribedBase>&) const override
			{
				return false;
			}
			bool get_enum(int, const RBX::Reflection::EnumDescriptor&, int&) const override
			{
				return false;
			}

			void* get(int) const override { return nullptr; }
		};
	} // namespace

	InstanceRef create_instance(const char* class_name, const CreatorRole role) noexcept
	{
		if (!class_name || !g_pointers)
			return {};

		const auto& pointers = g_pointers->m_roblox_pointers;
		if (!pointers.get_string_atom || !pointers.object_create_by_name)
		{
			LOG_ERROR("No instance creator resolved on this build");
			return {};
		}

		try
		{
			const auto atom = pointers.get_string_atom(class_name);
			if (!atom)
			{
				LOG_ERROR("No class atom for '{}'", class_name);
				return {};
			}

			struct Created
			{
				std::uintptr_t instance;
				std::uintptr_t control;
			};

			Created created{};
			memory::call_returning<Created>(reinterpret_cast<void*>(pointers.object_create_by_name), created,
			    std::uintptr_t{0}, static_cast<std::uintptr_t>(atom), static_cast<std::uint32_t>(role));

			if (!created.instance)
			{
				LOG_ERROR("Creating a '{}' returned nothing", class_name);
				return {};
			}

			const InstanceRef reference{
			    reinterpret_cast<void*>(created.instance), reinterpret_cast<void*>(created.control)};

			// The atom is only a name; what came back has to agree with it, or the
			// caller would be handed an instance of some other class.
			const auto* created_class = class_name_of(reference.instance);
			if (!created_class || std::string_view{created_class} != class_name)
			{
				LOG_ERROR("Asked for a '{}' and got a '{}'", class_name, created_class ? created_class : "?");
				release_instance(reference);
				return {};
			}

			return reference;
		}
		catch (const std::exception& e)
		{
			LOG_ERROR("Creating a '{}' raised: {}", class_name, e.what());
			return {};
		}
		catch (...)
		{
			LOG_ERROR("Creating a '{}' raised", class_name);
			return {};
		}
	}

	const char* class_name_of(void* instance) noexcept
	{
		if (!instance)
			return nullptr;

		try
		{
			return static_cast<RBX::Instance*>(instance)->get_descriptor().name.c_str();
		}
		catch (...)
		{
			return nullptr;
		}
	}

	bool set_parent(void* instance, void* parent) noexcept
	{
		if (!instance)
			return false;
		try
		{
			auto* const child = static_cast<RBX::Instance*>(instance);
			const auto* descriptor = child->get_descriptor().find_property("Parent");
			const auto* reference =
			    dynamic_cast<const RBX::Reflection::RefPropertyDescriptor*>(descriptor);
			if (!reference)
			{
				LOG_ERROR("Class '{}' has no Parent reference property", class_name_of(instance));
				return false;
			}
			reference->set_ref_value(child, static_cast<RBX::Instance*>(parent));
			return true;
		}
		catch (const std::exception& e)
		{
			LOG_ERROR("Reparenting an instance raised: {}", e.what());
			return false;
		}
		catch (...)
		{
			LOG_ERROR("Reparenting an instance raised");
			return false;
		}
	}

	bool set_name(void* instance, const char* name) noexcept
	{
		if (!instance || !name)
			return false;
		try
		{
			auto* const target = static_cast<RBX::Instance*>(instance);
			const auto* descriptor = target->get_descriptor().find_property("Name");
			if (!descriptor)
				return false;
			return descriptor->set_string_value(target, name);
		}
		catch (...)
		{
			LOG_ERROR("Renaming an instance raised");
			return false;
		}
	}

	bool destroy_instance(const InstanceRef reference) noexcept
	{
		if (!reference)
			return false;

		bool destroyed = false;

		try
		{
			auto* const instance = static_cast<RBX::Instance*>(reference.instance);

			// Reached by name instead of by vtable slot: `Destroy` is the member
			// the engine itself runs when a user deletes an instance, and a
			// renamed slot would be a silent wrong call.
			if (const auto* descriptor = instance->get_descriptor().find_function("Destroy"))
			{
				NoArguments arguments{};
				(void)RBX::Function(*descriptor, instance).invoke(arguments, false);
				destroyed = true;
			}
			else
			{
				LOG_ERROR("Class '{}' has no Destroy member", class_name_of(reference.instance));
			}
		}
		catch (const std::exception& e)
		{
			LOG_ERROR("Destroying an instance raised: {}", e.what());
		}
		catch (...)
		{
			LOG_ERROR("Destroying an instance raised");
		}

		release_instance(reference);

		return destroyed;
	}

	void release_instance(const InstanceRef reference) noexcept
	{
		if (!reference.control)
			return;

		auto** const vtable = *reinterpret_cast<void***>(reference.control);
		if (!vtable)
			return;

		if (uses_of(reference.control)->fetch_sub(1, std::memory_order_acq_rel) != 1)
			return;

		if (vtable[0])
			reinterpret_cast<void (*)(void*)>(vtable[0])(reference.control);

		if (weaks_of(reference.control)->fetch_sub(1, std::memory_order_acq_rel) != 1)
			return;

		if (vtable[1])
			reinterpret_cast<void (*)(void*)>(vtable[1])(reference.control);
	}
} // namespace rml::roblox
