#pragma once

#include "RobloxModLoader/rml_export.hpp"
#include "RobloxModLoader/roblox/reflection/property_accessor.hpp"

#include <memory>
#include <string_view>

namespace RBX::Reflection
{
	class EnumDescriptor;
}

namespace rml::reflection
{
	struct EnumSpec;

	class RML_EXPORT EnumBuilder
	{
	public:
		EnumBuilder(std::string_view name, const void* key, bool bind);
		~EnumBuilder();
		EnumBuilder(EnumBuilder&&) noexcept;
		EnumBuilder& operator=(EnumBuilder&&) noexcept;

		void description(std::string_view text);
		void item(std::string_view name, int value);
		void item_description(std::string_view text);
		void item_hidden();
		void item_deprecated(std::string_view message);
		const RBX::Reflection::EnumDescriptor* commit();

	private:
		std::unique_ptr<EnumSpec> m_spec;
	};

	template<ModEnum E>
	class TypedEnumBuilder;

	template<ModEnum E>
	class EnumItemBuilder
	{
	public:
		explicit EnumItemBuilder(TypedEnumBuilder<E>& parent) :
		    m_parent(parent)
		{
		}

		EnumItemBuilder& description(const std::string_view text)
		{
			m_parent.base().item_description(text);
			return *this;
		}

		EnumItemBuilder& hidden()
		{
			m_parent.base().item_hidden();
			return *this;
		}

		EnumItemBuilder& deprecated(const std::string_view message = {})
		{
			m_parent.base().item_deprecated(message);
			return *this;
		}

		EnumItemBuilder item(const std::string_view name, const E value)
		{
			return m_parent.item(name, value);
		}

		const RBX::Reflection::EnumDescriptor* commit()
		{
			return m_parent.commit();
		}

	private:
		TypedEnumBuilder<E>& m_parent;
	};

	template<ModEnum E>
	class TypedEnumBuilder
	{
	public:
		TypedEnumBuilder(const std::string_view name, const bool bind) :
		    m_builder(name, enum_key<E>(), bind)
		{
		}

		TypedEnumBuilder& description(const std::string_view text)
		{
			m_builder.description(text);
			return *this;
		}

		EnumItemBuilder<E> item(const std::string_view name, const E value)
		{
			m_builder.item(name, static_cast<int>(value));
			return EnumItemBuilder<E>(*this);
		}

		const RBX::Reflection::EnumDescriptor* commit()
		{
			return m_builder.commit();
		}

		EnumBuilder& base()
		{
			return m_builder;
		}

	private:
		EnumBuilder m_builder;
	};
}
