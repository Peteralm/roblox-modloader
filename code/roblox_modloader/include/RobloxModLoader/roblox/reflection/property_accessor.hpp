#pragma once

#include <cstdint>
#include <new>
#include <string>
#include <utility>

namespace rml::reflection
{
	enum class PropertyType : std::uint8_t
	{
		Bool,
		Int,
		Float,
		Double,
		String
	};

	template<typename T>
	struct property_type_of;

	template<>
	struct property_type_of<bool>
	{
		static constexpr PropertyType value = PropertyType::Bool;
	};

	template<>
	struct property_type_of<int>
	{
		static constexpr PropertyType value = PropertyType::Int;
	};

	template<>
	struct property_type_of<float>
	{
		static constexpr PropertyType value = PropertyType::Float;
	};

	template<>
	struct property_type_of<double>
	{
		static constexpr PropertyType value = PropertyType::Double;
	};

	template<>
	struct property_type_of<std::string>
	{
		static constexpr PropertyType value = PropertyType::String;
	};

	template<typename T>
	struct VariantOps
	{
		static void construct(const char* source, char* storage)
		{
			::new (storage) T(*reinterpret_cast<const T*>(source));
		}

		static void move_construct(char* source, char* storage)
		{
			::new (storage) T(std::move(*reinterpret_cast<T*>(source)));
		}

		static void destruct(char* storage)
		{
			reinterpret_cast<T*>(storage)->~T();
		}

		static inline const void* const table[3] = {
		    reinterpret_cast<const void*>(&construct),
		    reinterpret_cast<const void*>(&move_construct),
		    reinterpret_cast<const void*>(&destruct),
		};
	};

	template<typename T>
	class GetSet
	{
	public:
		virtual ~GetSet() = default;
		virtual bool is_read_only() const = 0;
		virtual bool is_write_only() const = 0;
		virtual T get_value(const void* instance) const = 0;
		virtual void set_value(void* instance, const T& value) const = 0;
		virtual bool equal_values(const void* a, const void* b) const = 0;
		virtual bool is_value_equal_to(const void* instance, const T& value) const = 0;
	};

	template<typename Class, typename T>
	class MemberGetSet final : public GetSet<T>
	{
	public:
		explicit MemberGetSet(T Class::* member) :
		    m_member(member)
		{
		}

		bool is_read_only() const override
		{
			return false;
		}

		bool is_write_only() const override
		{
			return false;
		}

		T get_value(const void* instance) const override
		{
			return static_cast<const Class*>(instance)->*m_member;
		}

		void set_value(void* instance, const T& value) const override
		{
			static_cast<Class*>(instance)->*m_member = value;
		}

		bool equal_values(const void* a, const void* b) const override
		{
			return get_value(a) == get_value(b);
		}

		bool is_value_equal_to(const void* instance, const T& value) const override
		{
			return get_value(instance) == value;
		}

	private:
		T Class::* m_member;
	};

	template<typename Class, typename T, typename Getter, typename Setter>
	class MethodGetSet final : public GetSet<T>
	{
	public:
		MethodGetSet(Getter getter, Setter setter) :
		    m_getter(getter),
		    m_setter(setter)
		{
		}

		bool is_read_only() const override
		{
			return m_setter == nullptr;
		}

		bool is_write_only() const override
		{
			return m_getter == nullptr;
		}

		T get_value(const void* instance) const override
		{
			return m_getter ? (static_cast<const Class*>(instance)->*m_getter)() : T{};
		}

		void set_value(void* instance, const T& value) const override
		{
			if (m_setter)
				(static_cast<Class*>(instance)->*m_setter)(value);
		}

		bool equal_values(const void* a, const void* b) const override
		{
			return get_value(a) == get_value(b);
		}

		bool is_value_equal_to(const void* instance, const T& value) const override
		{
			return get_value(instance) == value;
		}

	private:
		Getter m_getter;
		Setter m_setter;
	};

	template<typename Class, typename T>
	class FunctionGetSet final : public GetSet<T>
	{
	public:
		using Getter = T (*)(Class*);
		using Setter = void (*)(Class*, const T&);

		FunctionGetSet(Getter getter, Setter setter) :
		    m_getter(getter),
		    m_setter(setter)
		{
		}

		bool is_read_only() const override
		{
			return m_setter == nullptr;
		}

		bool is_write_only() const override
		{
			return m_getter == nullptr;
		}

		T get_value(const void* instance) const override
		{
			return m_getter ? m_getter(static_cast<Class*>(const_cast<void*>(instance))) : T{};
		}

		void set_value(void* instance, const T& value) const override
		{
			if (m_setter)
				m_setter(static_cast<Class*>(instance), value);
		}

		bool equal_values(const void* a, const void* b) const override
		{
			return get_value(a) == get_value(b);
		}

		bool is_value_equal_to(const void* instance, const T& value) const override
		{
			return get_value(instance) == value;
		}

	private:
		Getter m_getter;
		Setter m_setter;
	};
}
