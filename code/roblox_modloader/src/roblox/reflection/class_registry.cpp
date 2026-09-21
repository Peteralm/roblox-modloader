#include "class_registry.hpp"

#include "RobloxModLoader/hooking/vtable_index.hpp"
#include "RobloxModLoader/memory/foreign_call.hpp"
#include "RobloxModLoader/mod/init_context.hpp"
#include "RobloxModLoader/roblox/reflection/array_view.hpp"
#include "RobloxModLoader/roblox/reflection/class_builder.hpp"
#include "app/init_gate.hpp"
#include "pointers.hpp"

#include <cstring>

RML_LOG_SCOPE("ClassRegistry");

namespace rml::reflection
{
	static constexpr std::size_t k_class_descriptor_storage = 1024;
	static constexpr std::uint32_t k_protection_none = 0;
	static constexpr std::uint16_t k_functionality_persistent_local = 0x1 | 0x8 | 0x10;

	struct ClassAttributes
	{
		std::uint64_t descriptor_attributes[2]{};
		std::uint16_t functionality{k_functionality_persistent_local};
		std::uint8_t padding[6]{};
	};

	struct CreatedInstance
	{
		std::uintptr_t instance;
		std::uintptr_t control_block;
	};

	static constexpr std::size_t k_descriptor_field_offset = 0x18;

	static const RBX::Name* mod_get_class_name(const void* self)
	{
		const auto descriptor = *reinterpret_cast<const RBX::Reflection::ClassDescriptor* const*>(static_cast<const std::byte*>(self) + k_descriptor_field_offset);
		return &descriptor->name;
	}

	ClassRegistry& ClassRegistry::instance()
	{
		static ClassRegistry registry;
		return registry;
	}

	bool ClassRegistry::available()
	{
		if (!g_pointers)
			return false;

		const auto& p = g_pointers->m_roblox_pointers;
		return p.class_descriptor_ctor && p.class_descriptor_all_classes && p.creatable_get_creator && p.object_create_by_name && p.get_string_atom;
	}

	RBX::Reflection::ClassDescriptor* ClassRegistry::find_engine_class(std::string_view name) const
	{
		const auto all = g_pointers->m_roblox_pointers.class_descriptor_all_classes();
		if (!all)
			return nullptr;

		for (auto* descriptor : *all)
		{
			if (descriptor && descriptor->name.to_string() == name)
				return descriptor;
		}

		return nullptr;
	}

	std::expected<RBX::Reflection::ClassDescriptor*, std::string> ClassRegistry::define(const ClassSpec& spec)
	{
		if (!available())
			return std::unexpected("reflection registration is unavailable on this build");

		if (!g_init_gate || !g_init_gate->is_open())
			return std::unexpected("class registration is only possible inside on_init");

		if (spec.name.empty())
			return std::unexpected("class name is empty");

		if (find_engine_class(spec.name))
			return std::unexpected(std::format("class '{}' already exists", spec.name));

		auto* base = find_engine_class(spec.base);
		if (!base)
			return std::unexpected(std::format("base class '{}' not found", spec.base));

		auto& entry = m_classes.emplace_back();
		entry.name = spec.name;
		entry.storage = std::make_unique<std::byte[]>(k_class_descriptor_storage);
		std::memset(entry.storage.get(), 0, k_class_descriptor_storage);

		static ClassAttributes attributes;
		const auto& p = g_pointers->m_roblox_pointers;
		p.class_descriptor_ctor(entry.storage.get(), base, entry.name.c_str(), 0, 0, false, false, &attributes, k_protection_none, nullptr,
		    RBX::ArrayView<const RBX::Reflection::PropertyDescriptor*>{},
		    RBX::ArrayView<const RBX::Reflection::EventDescriptor*>{},
		    RBX::ArrayView<const RBX::Reflection::FunctionDescriptor*>{},
		    RBX::ArrayView<const RBX::Reflection::YieldFunctionDescriptor*>{},
		    RBX::ArrayView<const RBX::Reflection::CallbackDescriptor*>{});

		entry.descriptor = reinterpret_cast<RBX::Reflection::ClassDescriptor*>(entry.storage.get());
		entry.creator = std::make_unique<ModInstanceCreator>(entry.descriptor);
		m_creators[&entry.descriptor->name] = entry.creator.get();

		RML_INFO("Registered class {} : {} (descriptor 0x{:X}, name 0x{:X})", spec.name, spec.base,
		    reinterpret_cast<std::uintptr_t>(entry.descriptor), reinterpret_cast<std::uintptr_t>(&entry.descriptor->name));
		return entry.descriptor;
	}

	void** ClassRegistry::vtable_for(const RBX::Reflection::ClassDescriptor* descriptor, void** engine_vtable)
	{
		for (auto& entry : m_classes)
		{
			if (entry.descriptor != descriptor)
				continue;

			std::call_once(entry.vtable_once, [&] {
				entry.vtable = std::make_unique<ClonedVtable>();
				std::memcpy(entry.vtable->data(), engine_vtable - k_vtable_prefix_slots, sizeof(ClonedVtable));
				const auto slot = vtable_index_of(&RBX::Reflection::DescribedBase::get_class_name);
				(*entry.vtable)[k_vtable_prefix_slots + slot] = reinterpret_cast<void*>(&mod_get_class_name);
				RML_INFO("Cloned vtable for {} from 0x{:X}; get_class_name at slot {}", entry.name, reinterpret_cast<std::uintptr_t>(engine_vtable), slot);
			});
			return entry.vtable->data() + k_vtable_prefix_slots;
		}

		return engine_vtable;
	}

	const RBX::ICreator* ClassRegistry::creator_for(const RBX::Name* name) const
	{
		const auto it = m_creators.find(name);
		return it == m_creators.end() ? nullptr : it->second;
	}

	std::shared_ptr<void> ModInstanceCreator::create(RBX::EngineContext* context, RBX::CreatorRole role) const
	{
		const auto& p = g_pointers->m_roblox_pointers;
		const auto folder = p.get_string_atom("Folder");

		CreatedInstance created{};
		memory::call_returning<CreatedInstance>(reinterpret_cast<void*>(p.object_create_by_name), created,
		    reinterpret_cast<std::uintptr_t>(context), static_cast<std::uintptr_t>(folder), static_cast<std::uint32_t>(RBX::CreatorRole::Engine));

		std::shared_ptr<void> result;
		static_assert(sizeof(result) == sizeof(created));
		if (!created.instance)
			return result;

		*reinterpret_cast<const RBX::Reflection::ClassDescriptor**>(created.instance + k_descriptor_field_offset) = m_descriptor;
		auto& vptr = *reinterpret_cast<void***>(created.instance);
		vptr = ClassRegistry::instance().vtable_for(m_descriptor, vptr);
		std::memcpy(&result, &created, sizeof(created));
		return result;
	}

	bool ModInstanceCreator::is_serializable() const
	{
		return false;
	}

	bool ModInstanceCreator::is_script_creatable() const
	{
		return true;
	}
}

namespace rml::reflection
{
	ClassBuilder::ClassBuilder(std::string_view name, std::string_view base) :
	    m_spec(std::make_unique<ClassSpec>(std::string(name), std::string(base)))
	{
	}

	ClassBuilder::~ClassBuilder() = default;
	ClassBuilder::ClassBuilder(ClassBuilder&&) noexcept = default;
	ClassBuilder& ClassBuilder::operator=(ClassBuilder&&) noexcept = default;

	ClassBuilder& ClassBuilder::base(std::string_view engine_class)
	{
		m_spec->base = engine_class;
		return *this;
	}

	void ClassBuilder::commit()
	{
		if (const auto result = ClassRegistry::instance().define(*m_spec); !result)
			throw std::logic_error(result.error());
	}
}

namespace rml
{
	reflection::ClassBuilder InitContext::define_class(std::string_view name, std::string_view base)
	{
		return reflection::ClassBuilder(name, base);
	}
}
