#pragma once

#include "RobloxModLoader/rml_export.hpp"
#include "RobloxModLoader/roblox/reflection/described_creatable.hpp"
#include "RobloxModLoader/roblox/reflection/property_accessor.hpp"

#include <cstddef>
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
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

	class RML_EXPORT ClassBuilder
	{
	public:
		ClassBuilder(std::string_view name, std::string_view base, const ClassLayout& layout);
		~ClassBuilder();
		ClassBuilder(ClassBuilder&&) noexcept;
		ClassBuilder& operator=(ClassBuilder&&) noexcept;

		ClassBuilder& property(std::string_view name, PropertyType type, std::shared_ptr<void> accessor, std::string_view category);
		ClassBuilder& function(std::string_view name, std::shared_ptr<FunctionInvoker> invoker);
		ClassBuilder& event(std::string_view name, std::ptrdiff_t member_offset, std::vector<EventArgument> arguments);
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

		ExtensionBuilder& property(std::string_view name, PropertyType type, std::shared_ptr<void> accessor, std::string_view category);
		ExtensionBuilder& function(std::string_view name, std::shared_ptr<FunctionInvoker> invoker);
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
		TypedExtensionBuilder& property(std::string_view name, T (*getter)(Base*), void (*setter)(Base*, const T&), std::string_view category = "Data")
		{
			m_builder.property(name, property_type_of<T>::value, std::make_shared<FunctionGetSet<Base, T>>(getter, setter), category);
			return *this;
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

		template<typename T>
		TypedClassBuilder& property(std::string_view name, T Derived::* member, std::string_view category = "Data")
		{
			m_builder.property(name, property_type_of<T>::value, std::make_shared<MemberGetSet<Derived, T>>(member), category);
			return *this;
		}

		template<typename Getter, typename Setter>
		TypedClassBuilder& property(std::string_view name, Getter getter, Setter setter, std::string_view category = "Data")
		{
			using T = std::remove_cvref_t<std::invoke_result_t<Getter, const Derived&>>;
			m_builder.property(name, property_type_of<T>::value, std::make_shared<MethodGetSet<Derived, T, Getter, Setter>>(getter, setter), category);
			return *this;
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
