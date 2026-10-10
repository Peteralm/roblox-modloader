#pragma once

#include <cstdint>
#include <initializer_list>
#include <type_traits>

namespace rml
{
	template<typename Enum>
	class Flags
	{
	public:
		using Bits = std::uint64_t;

		constexpr Flags() = default;

		constexpr Flags(const Enum value) :
		    m_bits(bit(value))
		{
		}

		constexpr Flags(const std::initializer_list<Enum> values)
		{
			for (const auto value : values)
				m_bits |= bit(value);
		}

		[[nodiscard]] constexpr bool contains(const Enum value) const
		{
			return (m_bits & bit(value)) != 0;
		}

		[[nodiscard]] constexpr bool empty() const
		{
			return m_bits == 0;
		}

		[[nodiscard]] constexpr Bits bits() const
		{
			return m_bits;
		}

		constexpr Flags& operator|=(const Flags other)
		{
			m_bits |= other.m_bits;
			return *this;
		}

		[[nodiscard]] friend constexpr Flags operator|(Flags lhs, const Flags rhs)
		{
			return lhs |= rhs;
		}

		friend constexpr bool operator==(const Flags&, const Flags&) = default;

	private:
		static constexpr Bits bit(const Enum value)
		{
			return Bits{1} << static_cast<std::underlying_type_t<Enum>>(value);
		}

		Bits m_bits{};
	};
}
