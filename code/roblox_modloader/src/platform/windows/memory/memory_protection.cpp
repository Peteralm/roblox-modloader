#include "RobloxModLoader/internal/common.hpp"
#include "RobloxModLoader/platform/memory/memory_protection.hpp"
#include "utils/memory_protection_guard.hpp"

namespace rml::platform
{
	std::expected<unsigned long, std::error_code> set_protection(void* address, const std::size_t size, const utils::MemoryProtection protection)
	{
		DWORD new_protect = PAGE_READWRITE;

		switch (protection)
		{
		case utils::MemoryProtection::ReadWrite:
			new_protect = PAGE_READWRITE;
			break;
		case utils::MemoryProtection::ExecuteReadWrite:
			new_protect = PAGE_EXECUTE_READWRITE;
			break;
		}

		DWORD old_protect = 0;
		if (!VirtualProtect(address, size, new_protect, &old_protect))
			return std::unexpected(std::error_code(static_cast<int>(GetLastError()), std::system_category()));

		return old_protect;
	}

	void restore_protection(void* address, const std::size_t size, const unsigned long previous) noexcept
	{
		DWORD temp = 0;
		VirtualProtect(address, size, static_cast<DWORD>(previous), &temp);
	}

	bool is_executable(const void* address) noexcept
	{
		MEMORY_BASIC_INFORMATION info{};
		if (!address || !VirtualQuery(address, &info, sizeof(info)) || info.State != MEM_COMMIT)
			return false;
		return (info.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
	}

	bool is_readable(const void* address, const std::size_t size) noexcept
	{
		static constexpr DWORD k_readable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
		MEMORY_BASIC_INFORMATION info{};
		if (!address || !VirtualQuery(address, &info, sizeof(info)) || info.State != MEM_COMMIT || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0 || (info.Protect & k_readable) == 0)
			return false;
		const auto start = reinterpret_cast<std::uintptr_t>(address);
		const auto end = reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
		return size <= end - start;
	}
}
