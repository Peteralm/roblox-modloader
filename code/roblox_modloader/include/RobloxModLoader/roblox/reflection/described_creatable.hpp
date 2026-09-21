#pragma once

#include "RobloxModLoader/hooking/vtable_index.hpp"
#include "RobloxModLoader/roblox/instance.hpp"

#include <cstddef>
#include <new>

namespace rml::reflection
{
	struct ClassLayout
	{
		std::size_t size;
		std::size_t align;
		std::size_t virtual_slots;
		void (*construct)(void* memory);
		void (*destroy)(void* object);
		const void* const* base_vtable;
	};

	template<typename Base>
	struct engine_virtual_slots;

	template<>
	struct engine_virtual_slots<RBX::Instance>
	{
		static std::size_t value()
		{
			return vtable_index_of(&RBX::Instance::post_equality_check_name_callback_for_subclass) + 1;
		}
	};

	template<typename Derived>
	class TypedClassBuilder;

	template<typename Derived, typename Base = RBX::Instance>
	class DescribedCreatable : public Base
	{
	public:
		using BaseClass = Base;

		static const RBX::Reflection::ClassDescriptor* class_descriptor()
		{
			return s_descriptor;
		}

		const RBX::Name& get_class_name() const override
		{
			return this->get_descriptor().name;
		}

		static ClassLayout layout()
		{
			return {sizeof(Derived), alignof(Derived), engine_virtual_slots<Base>::value(), &construct, &destroy, base_vtable()};
		}

	private:
		struct BaseProbe final : Base
		{
		};

		static const void* const* base_vtable()
		{
			alignas(BaseProbe) static std::byte storage[sizeof(BaseProbe)];
			static const void* const* const vtable = *reinterpret_cast<const void* const**>(::new (storage) BaseProbe);
			return vtable;
		}

		static void construct(void* memory)
		{
			::new (memory) Derived;
		}

		static void destroy(void* object)
		{
			static_cast<Derived*>(object)->~Derived();
		}

		static inline const RBX::Reflection::ClassDescriptor* s_descriptor{};

		friend class TypedClassBuilder<Derived>;
	};
}
