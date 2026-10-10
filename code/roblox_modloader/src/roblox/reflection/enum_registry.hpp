#pragma once

#include "RobloxModLoader/memory/vtable.hpp"
#include "RobloxModLoader/roblox/reflection/enum_descriptor.hpp"
#include "RobloxModLoader/roblox/reflection/hints.hpp"

#include <array>
#include <deque>
#include <expected>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace rml::reflection
{
	struct EnumItemSpec
	{
		std::string name;
		int value;
		EnumItemHints hints;
	};

	struct EnumSpec
	{
		std::string name;
		const void* key{};
		bool bind{};
		EnumHints hints;
		std::vector<EnumItemSpec> items;
	};

	class EnumRegistry
	{
	public:
		static EnumRegistry& instance();
		[[nodiscard]] static bool available();
		[[nodiscard]] static const RBX::Reflection::EnumDescriptor* find_engine(std::string_view name);

		[[nodiscard]] std::expected<const RBX::Reflection::EnumDescriptor*, std::string> commit(const EnumSpec& spec);
		[[nodiscard]] const RBX::Reflection::EnumDescriptor* find(const void* key) const;

	private:
		struct ModEnum
		{
			alignas(RBX::Reflection::EnumDescriptor) std::array<std::byte, sizeof(RBX::Reflection::EnumDescriptor)> descriptor{};
			std::unique_ptr<std::byte[]> items;
			std::vector<RBX::Reflection::EnumDescriptor::NameEntry> by_name;
			std::vector<RBX::Reflection::EnumDescriptor::ValueEntry> by_value;
			memory::VtableCopy vtable;
		};

		[[nodiscard]] std::expected<const RBX::Reflection::EnumDescriptor*, std::string> define(const EnumSpec& spec);
		[[nodiscard]] static std::expected<const RBX::Reflection::EnumDescriptor*, std::string> bind(const EnumSpec& spec);

		mutable std::mutex m_mutex;
		std::deque<std::unique_ptr<ModEnum>> m_enums;
		std::unordered_map<const void*, const RBX::Reflection::EnumDescriptor*> m_by_key;
	};
}
