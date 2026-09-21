#include "mod_descriptors.hpp"

#include "RobloxModLoader/memory/i_rtti_provider.hpp"
#include "RobloxModLoader/memory/pattern.hpp"
#include "RobloxModLoader/memory/range.hpp"
#include "RobloxModLoader/memory/string_anchor.hpp"
#include "pointers.hpp"

#include <cstring>
#include <mutex>
#include <unordered_map>

RML_LOG_SCOPE("ModDescriptors");

namespace rml::reflection
{
	static constexpr std::uint32_t k_protection_none = 0;

	static std::mutex s_functions_mutex;
	static std::unordered_map<const void*, const FunctionInvoker*> s_functions;

	static const FunctionInvoker* invoker_for(const void* descriptor)
	{
		std::lock_guard lock(s_functions_mutex);
		const auto it = s_functions.find(descriptor);
		return it == s_functions.end() ? nullptr : it->second;
	}

	class FunctionCarrier
	{
	public:
		virtual ~FunctionCarrier() = default;

		virtual int execute_custom(void*, lua_State*) const
		{
			return 0;
		}

		virtual int execute_lua(void* instance, lua_State* L, int) const
		{
			const auto invoker = invoker_for(this);
			if (!invoker)
				return 0;

			try
			{
				return invoker->invoke(static_cast<RBX::Instance*>(instance), L);
			}
			catch (const std::exception& e)
			{
				g_pointers->m_roblox_pointers.luaL_errorL(L, "%s", e.what());
			}
			catch (...)
			{
				g_pointers->m_roblox_pointers.luaL_errorL(L, "unknown error in mod function");
			}
			return 0;
		}
	};

	static void* function_carrier_vtable()
	{
		static const FunctionCarrier carrier;
		return *reinterpret_cast<void* const*>(&carrier);
	}

	const PropertyTypeInfo& property_type_info(const PropertyType type)
	{
		static constexpr PropertyTypeInfo infos[] = {
		    {"bool", "RBX::Reflection::TypedPropertyDescriptor<bool>", RBX::Reflection::TypeId::Bool, true, false},
		    {"int", "RBX::Reflection::TypedPropertyDescriptor<int>", RBX::Reflection::TypeId::Int, true, false},
		    {"float", "RBX::Reflection::TypedPropertyDescriptor<float>", RBX::Reflection::TypeId::Float, true, true},
		    {"double", "RBX::Reflection::TypedPropertyDescriptor<double>", RBX::Reflection::TypeId::Double, true, true},
		    {"string", "RBX::Reflection::TypedPropertyDescriptor<std::string>", RBX::Reflection::TypeId::String, false, false},
		};
		return infos[static_cast<std::size_t>(type)];
	}

#if defined(RML_WINDOWS)
	static const RBX::Reflection::Type* find_type_singleton(const PropertyTypeInfo& info)
	{
		return nullptr;
	}
#else
	static const RBX::Reflection::Type* find_type_singleton(const PropertyTypeInfo& info)
	{
		const auto mov_w = [](const unsigned reg, const unsigned value) { return std::format("{:02X} {:02X} 80 52", ((value << 5) | reg) & 0xFF, (value << 5) >> 8); };
		const memory::pattern shape(
		    "F4 4F BE A9 FD 7B 01 A9 FD 43 00 91 ? ? ? ? ? ? ? ? 08 C1 BF 38 ? ? ? ? ? ? ? ? ? ? ? ? FD 7B 41 A9 F4 4F C2 A8 C0 03 5F D6 "
		    "? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? "
		    + mov_w(2, info.type_id) + " " + mov_w(3, info.is_number) + " " + mov_w(4, info.is_float) + " 05 00 80 52 06 00 80 52");

		for (const auto& function : memory::functions_referencing_string(info.engine_name))
		{
			const memory::range body(memory::handle(function.start), function.size);
			const auto hit = body.scan(shape);
			if (!hit || hit->as<void*>() != function.start)
				continue;

			RML_DEBUG("Type::getSingleton<{}> at 0x{:X}", info.engine_name, reinterpret_cast<std::uintptr_t>(function.start));
			return reinterpret_cast<const RBX::Reflection::Type* (*)()>(function.start)();
		}

		return nullptr;
	}
#endif

	static const RBX::Reflection::Type* type_singleton(const PropertyType type)
	{
		static std::mutex mutex;
		static std::unordered_map<PropertyType, const RBX::Reflection::Type*> cache;

		std::lock_guard lock(mutex);
		if (const auto it = cache.find(type); it != cache.end())
			return it->second;

		const auto singleton = find_type_singleton(property_type_info(type));
		cache[type] = singleton;
		return singleton;
	}

	static void* typed_property_vtable(const PropertyType type)
	{
		static std::mutex mutex;
		static std::unordered_map<PropertyType, void*> cache;

		std::lock_guard lock(mutex);
		if (const auto it = cache.find(type); it != cache.end())
			return it->second;

		void* vtable = nullptr;
		if (g_rtti_provider)
		{
			if (const auto found = g_rtti_provider->find_class_vtable(property_type_info(type).descriptor_class))
				vtable = *found;
		}
		cache[type] = vtable;
		return vtable;
	}

	std::expected<ModMember, std::string> make_property(void* owner_storage, const std::string& name, const std::string& category, const PropertyType type, void* accessor)
	{
		const auto& p = g_pointers->m_roblox_pointers;
		if (!p.property_descriptor_ctor)
			return std::unexpected("PROPERTY_DESCRIPTOR_CTOR is unavailable");

		const auto& info = property_type_info(type);
		const auto engine_type = type_singleton(type);
		if (!engine_type)
			return std::unexpected(std::format("Type::getSingleton<{}> was not found; property '{}' skipped", info.engine_name, name));

		const auto vtable = typed_property_vtable(type);
		if (!vtable)
			return std::unexpected(std::format("no vtable for {}; property '{}' skipped", info.descriptor_class, name));

		ModMember member;
		member.storage = std::make_unique<std::byte[]>(k_member_storage);
		std::memset(member.storage.get(), 0, k_member_storage);

		static PropertyAttributes attributes;
		auto* bytes = member.storage.get();
		p.property_descriptor_ctor(bytes, owner_storage, engine_type, name.c_str(), category.c_str(), &attributes, k_protection_none, k_protection_none, false);
		*reinterpret_cast<void**>(bytes) = vtable;
		*reinterpret_cast<void**>(bytes + k_property_accessor_offset) = accessor;
		*reinterpret_cast<void**>(bytes + k_property_accessor_offset + 8) = nullptr;
		*reinterpret_cast<void**>(bytes + k_property_accessor_offset + 16) = nullptr;

		return member;
	}

	std::expected<ModMember, std::string> make_function(void* owner_storage, const std::string& name, const FunctionInvoker* invoker)
	{
		const auto& p = g_pointers->m_roblox_pointers;
		if (!p.function_descriptor_ctor)
			return std::unexpected("FUNCTION_DESCRIPTOR_CTOR is unavailable");

		ModMember member;
		member.storage = std::make_unique<std::byte[]>(k_member_storage);
		std::memset(member.storage.get(), 0, k_member_storage);

		auto* bytes = member.storage.get();
		p.function_descriptor_ctor(bytes, owner_storage, name.c_str(), k_protection_none, 0, 0);
		*reinterpret_cast<void**>(bytes) = function_carrier_vtable();

		{
			std::lock_guard lock(s_functions_mutex);
			s_functions[bytes] = invoker;
		}

		return member;
	}
}
