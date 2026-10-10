#pragma once

#include "RobloxModLoader/rml_export.hpp"
#include "RobloxModLoader/roblox/reflection/described_creatable.hpp"
#include "RobloxModLoader/roblox/reflection/hints.hpp"
#include "RobloxModLoader/roblox/reflection/property_accessor.hpp"

#include <cstddef>
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

struct lua_State;

namespace RBX
{
	class Instance;

	namespace Reflection
	{
		class ClassDescriptor;
	}
}

namespace rml::reflection
{
	struct ClassSpec;

	class FunctionInvoker
	{
	public:
		virtual ~FunctionInvoker() = default;
		virtual int invoke(RBX::Instance* instance, lua_State* L) const = 0;
	};

	template<typename Class>
	class MethodInvoker final : public FunctionInvoker
	{
	public:
		using Method = int (Class::*)(lua_State*);

		explicit MethodInvoker(Method method) :
		    m_method(method)
		{
		}

		int invoke(RBX::Instance* instance, lua_State* L) const override
		{
			return (static_cast<Class*>(instance)->*m_method)(L);
		}

	private:
		Method m_method;
	};

	template<typename Class>
	class FunctionInvokerFn final : public FunctionInvoker
	{
	public:
		using Function = int (*)(Class*, lua_State*);

		explicit FunctionInvokerFn(Function function) :
		    m_function(function)
		{
		}

		int invoke(RBX::Instance* instance, lua_State* L) const override
		{
			return m_function(static_cast<Class*>(instance), L);
		}

	private:
		Function m_function;
	};

	struct EventArgument
	{
		PropertyType type;
		std::string name;
	};

	struct PropertySpec;

	class RML_EXPORT PropertyOptions
	{
	public:
		PropertyOptions(std::vector<PropertySpec>& properties, std::size_t index);

		void category(std::string_view name) const;
		void description(std::string_view text) const;
		void order(int value) const;
		void read_only() const;
		void hidden() const;
		void deprecated(std::string_view message) const;
		void slider(double min, double max, int ticks, SliderScaling scaling) const;
		void enum_type(const void* key) const;

	private:
		PropertySpec& spec() const;

		std::vector<PropertySpec>* m_properties;
		std::size_t m_index;
	};

	template<typename Parent, typename T>
	class PropertyBuilder
	{
	public:
		PropertyBuilder(Parent& parent, const PropertyOptions options) :
		    m_parent(parent),
		    m_options(options)
		{
		}

		PropertyBuilder& category(const std::string_view name)
		{
			m_options.category(name);
			return *this;
		}

		PropertyBuilder& description(const std::string_view text)
		{
			m_options.description(text);
			return *this;
		}

		PropertyBuilder& order(const int value)
		{
			m_options.order(value);
			return *this;
		}

		PropertyBuilder& read_only()
		{
			m_options.read_only();
			return *this;
		}

		PropertyBuilder& hidden()
		{
			m_options.hidden();
			return *this;
		}

		PropertyBuilder& deprecated(const std::string_view message = {})
		{
			m_options.deprecated(message);
			return *this;
		}

		PropertyBuilder& slider(const T min, const T max, const int ticks = 0, const SliderScaling scaling = SliderScaling::Linear)
		    requires(std::is_arithmetic_v<T> && !std::is_same_v<T, bool>)
		{
			m_options.slider(static_cast<double>(min), static_cast<double>(max), ticks, scaling);
			return *this;
		}

		template<typename... Args>
		decltype(auto) property(Args&&... args)
		{
			return m_parent.property(std::forward<Args>(args)...);
		}

		template<typename... Args>
		decltype(auto) function(Args&&... args)
		{
			return m_parent.function(std::forward<Args>(args)...);
		}

		template<typename... Args>
		decltype(auto) event(Args&&... args)
		{
			return m_parent.event(std::forward<Args>(args)...);
		}

		decltype(auto) commit()
		{
			return m_parent.commit();
		}

	private:
		Parent& m_parent;
		PropertyOptions m_options;
	};

	class RML_EXPORT ClassBuilder
	{
	public:
		ClassBuilder(std::string_view name, std::string_view base, const ClassLayout& layout);
		~ClassBuilder();
		ClassBuilder(ClassBuilder&&) noexcept;
		ClassBuilder& operator=(ClassBuilder&&) noexcept;

		ClassBuilder& property(std::string_view name, PropertyType type, std::shared_ptr<void> accessor);
		ClassBuilder& function(std::string_view name, std::shared_ptr<FunctionInvoker> invoker);
		ClassBuilder& event(std::string_view name, std::ptrdiff_t member_offset, std::vector<EventArgument> arguments);
		[[nodiscard]] PropertyOptions last_property();

		ClassBuilder& description(std::string_view text);
		ClassBuilder& insert_category(std::string_view name);
		ClassBuilder& explorer_order(int value);
		ClassBuilder& preferred_parent(std::string_view class_name);
		ClassBuilder& insertable(bool value);
		ClassBuilder& browsable(bool value);
		ClassBuilder& icon_of(std::string_view engine_class);

		const RBX::Reflection::ClassDescriptor* commit();

	private:
		std::unique_ptr<ClassSpec> m_spec;
	};

	struct ExtensionSpec;

	class RML_EXPORT ExtensionBuilder
	{
	public:
		explicit ExtensionBuilder(std::string_view class_name);
		~ExtensionBuilder();
		ExtensionBuilder(ExtensionBuilder&&) noexcept;
		ExtensionBuilder& operator=(ExtensionBuilder&&) noexcept;

		ExtensionBuilder& property(std::string_view name, PropertyType type, std::shared_ptr<void> accessor);
		ExtensionBuilder& function(std::string_view name, std::shared_ptr<FunctionInvoker> invoker);
		[[nodiscard]] PropertyOptions last_property();
		const RBX::Reflection::ClassDescriptor* commit();

	private:
		std::unique_ptr<ExtensionSpec> m_spec;
	};

	template<typename Base>
	class TypedExtensionBuilder
	{
	public:
		explicit TypedExtensionBuilder(std::string_view class_name) :
		    m_builder(class_name)
		{
		}

		template<typename T>
		PropertyBuilder<TypedExtensionBuilder, T> property(std::string_view name, T (*getter)(Base*), void (*setter)(Base*, const T&))
		{
			m_builder.property(name, property_type_of<T>::value, std::make_shared<FunctionGetSet<Base, T>>(getter, setter));
			if constexpr (ModEnum<T>)
				m_builder.last_property().enum_type(enum_key<T>());
			return PropertyBuilder<TypedExtensionBuilder, T>(*this, m_builder.last_property());
		}

		TypedExtensionBuilder& function(std::string_view name, int (*function)(Base*, lua_State*))
		{
			m_builder.function(name, std::make_shared<FunctionInvokerFn<Base>>(function));
			return *this;
		}

		const RBX::Reflection::ClassDescriptor* commit()
		{
			return m_builder.commit();
		}

	private:
		ExtensionBuilder m_builder;
	};

	template<typename Derived>
	class TypedClassBuilder
	{
	public:
		TypedClassBuilder(std::string_view name, std::string_view base) :
		    m_builder(name, base, Derived::layout())
		{
		}

		TypedClassBuilder& description(const std::string_view text)
		{
			m_builder.description(text);
			return *this;
		}

		TypedClassBuilder& insert_category(const std::string_view name)
		{
			m_builder.insert_category(name);
			return *this;
		}

		TypedClassBuilder& explorer_order(const int value)
		{
			m_builder.explorer_order(value);
			return *this;
		}

		TypedClassBuilder& preferred_parent(const std::string_view class_name)
		{
			m_builder.preferred_parent(class_name);
			return *this;
		}

		TypedClassBuilder& insertable(const bool value)
		{
			m_builder.insertable(value);
			return *this;
		}

		TypedClassBuilder& browsable(const bool value)
		{
			m_builder.browsable(value);
			return *this;
		}

		TypedClassBuilder& icon_of(const std::string_view engine_class)
		{
			m_builder.icon_of(engine_class);
			return *this;
		}

		template<typename T>
		PropertyBuilder<TypedClassBuilder, T> property(std::string_view name, T Derived::* member)
		{
			m_builder.property(name, property_type_of<T>::value, std::make_shared<MemberGetSet<Derived, T>>(member));
			if constexpr (ModEnum<T>)
				m_builder.last_property().enum_type(enum_key<T>());
			return PropertyBuilder<TypedClassBuilder, T>(*this, m_builder.last_property());
		}

		template<typename Getter, typename Setter>
		auto property(std::string_view name, Getter getter, Setter setter)
		{
			using T = std::remove_cvref_t<std::invoke_result_t<Getter, const Derived&>>;
			m_builder.property(name, property_type_of<T>::value, std::make_shared<MethodGetSet<Derived, T, Getter, Setter>>(getter, setter));
			if constexpr (ModEnum<T>)
				m_builder.last_property().enum_type(enum_key<T>());
			return PropertyBuilder<TypedClassBuilder, T>(*this, m_builder.last_property());
		}

		TypedClassBuilder& function(std::string_view name, int (Derived::*method)(lua_State*))
		{
			m_builder.function(name, std::make_shared<MethodInvoker<Derived>>(method));
			return *this;
		}

		template<typename... Args>
		TypedClassBuilder& event(std::string_view name, rbx::signal<void(Args...)> Derived::* member, std::initializer_list<std::string_view> argument_names = {})
		{
			std::vector<EventArgument> arguments{EventArgument{property_type_of<Args>::value, {}}...};
			std::size_t index = 0;
			for (const auto argument_name : argument_names)
			{
				if (index < arguments.size())
					arguments[index++].name = argument_name;
			}
			m_builder.event(name, member_offset(member), std::move(arguments));
			return *this;
		}

		void commit()
		{
			Derived::s_descriptor = m_builder.commit();
		}

	private:
		ClassBuilder m_builder;
	};
}
