#include "bootstrap_detour.hpp"

#include <Windows.h>
#include <cstring>

namespace rml::platform::windows
{
	namespace
	{
		void write_absolute_jump(std::byte* destination, const void* target) noexcept
		{
			const std::array prefix{std::byte{0xFF}, std::byte{0x25}, std::byte{0x00}, std::byte{0x00},
			    std::byte{0x00}, std::byte{0x00}};
			std::memcpy(destination, prefix.data(), prefix.size());
			std::memcpy(destination + prefix.size(), &target, sizeof(target));
		}

		bool write_code(std::byte* destination, const std::byte* source, const std::size_t size) noexcept
		{
			DWORD previous{};
			if (!VirtualProtect(destination, size, PAGE_EXECUTE_READWRITE, &previous))
				return false;
			std::memcpy(destination, source, size);
			FlushInstructionCache(GetCurrentProcess(), destination, size);
			DWORD ignored{};
			VirtualProtect(destination, size, previous, &ignored);
			return true;
		}
	}

	bool BootstrapDetour::arm(void* target, void* replacement, const std::uint8_t overwrite_size) noexcept
	{
		if (m_armed || !target || !replacement || overwrite_size != kPatchSize)
			return false;

		m_target = static_cast<std::byte*>(target);
		std::memcpy(m_original.data(), m_target, m_original.size());

		constexpr auto trampoline_size = kPatchSize + kPatchSize;
		m_trampoline = static_cast<std::byte*>(VirtualAlloc(
		    nullptr, trampoline_size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
		if (!m_trampoline)
			return false;
		std::memcpy(m_trampoline, m_original.data(), m_original.size());
		write_absolute_jump(m_trampoline + kPatchSize, m_target + kPatchSize);
		FlushInstructionCache(GetCurrentProcess(), m_trampoline, trampoline_size);

		std::array<std::byte, kPatchSize> patch{};
		write_absolute_jump(patch.data(), replacement);
		if (!write_code(m_target, patch.data(), patch.size()))
			return false;
		m_armed = true;
		return true;
	}

	bool BootstrapDetour::restore() noexcept
	{
		if (!m_armed || !m_target)
			return false;
		if (!write_code(m_target, m_original.data(), m_original.size()))
			return false;
		m_armed = false;
		return true;
	}

	BootstrapDetour::Target BootstrapDetour::original_entry() const noexcept
	{
		return reinterpret_cast<Target>(m_target);
	}

	BootstrapDetour::Target BootstrapDetour::trampoline() const noexcept
	{
		return reinterpret_cast<Target>(m_trampoline);
	}
} // namespace rml::platform::windows
