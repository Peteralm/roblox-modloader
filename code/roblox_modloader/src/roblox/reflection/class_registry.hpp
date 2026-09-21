#pragma once

#include "RobloxModLoader/internal/common.hpp"
#include "RobloxModLoader/roblox/reflection/creatable.hpp"
#include "RobloxModLoader/roblox/reflection/object.hpp"

#include <array>
#include <deque>
#include <expected>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace rml::reflection
{
	struct ClassSpec
	{
		std::string name;
		std::string base{"Instance"};
		std::size_t payload_size{0};
	};

	class ModInstanceCreator final : public RBX::ICreator
	{
	public:
		explicit ModInstanceCreator(RBX::Reflection::ClassDescriptor* descriptor) :
		    m_descriptor(descriptor)
		{
		}

		std::shared_ptr<void> create(RBX::EngineContext* context, RBX::CreatorRole role) const override;
		bool is_serializable() const override;
		bool is_script_creatable() const override;

	private:
		RBX::Reflection::ClassDescriptor* m_descriptor;
	};

	inline constexpr std::size_t k_vtable_prefix_slots = 2;
	inline constexpr std::size_t k_cloned_vtable_slots = 160;
	using ClonedVtable = std::array<void*, k_vtable_prefix_slots + k_cloned_vtable_slots>;

	struct RegisteredClass
	{
		std::string name;
		RBX::Reflection::ClassDescriptor* descriptor{};
		std::unique_ptr<ModInstanceCreator> creator;
		std::unique_ptr<std::byte[]> storage;
		std::unique_ptr<ClonedVtable> vtable;
		std::once_flag vtable_once;
	};

	class ClassRegistry
	{
	public:
		static ClassRegistry& instance();

		[[nodiscard]] static bool available();
		[[nodiscard]] std::expected<RBX::Reflection::ClassDescriptor*, std::string> define(const ClassSpec& spec);
		[[nodiscard]] const RBX::ICreator* creator_for(const RBX::Name* name) const;
		[[nodiscard]] RBX::Reflection::ClassDescriptor* find_engine_class(std::string_view name) const;
		[[nodiscard]] void** vtable_for(const RBX::Reflection::ClassDescriptor* descriptor, void** engine_vtable);
		[[nodiscard]] void* payload_for(const void* instance, const RBX::Reflection::ClassDescriptor* descriptor);
		void forget(const void* instance);

	private:
		std::deque<RegisteredClass> m_classes;
		std::unordered_map<const RBX::Name*, const RBX::ICreator*> m_creators;
		std::unordered_map<const RBX::Reflection::ClassDescriptor*, std::size_t> m_payload_sizes;
		std::mutex m_payload_mutex;
		std::unordered_map<const void*, std::unique_ptr<std::byte[]>> m_payloads;
	};
}
