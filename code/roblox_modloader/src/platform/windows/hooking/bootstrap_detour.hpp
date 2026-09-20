#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace rml::platform::windows
{
	class BootstrapDetour
	{
	public:
		static constexpr std::size_t kPatchSize = 14;
		using Target = void (*)();

		BootstrapDetour() = default;
		BootstrapDetour(const BootstrapDetour&) = delete;
		BootstrapDetour& operator=(const BootstrapDetour&) = delete;

		[[nodiscard]] bool arm(void* target, void* replacement, std::uint8_t overwrite_size) noexcept;
		[[nodiscard]] bool restore() noexcept;
		[[nodiscard]] Target original_entry() const noexcept;
		[[nodiscard]] Target trampoline() const noexcept;
		[[nodiscard]] bool is_armed() const noexcept { return m_armed; }

	private:
		std::byte* m_target{};
		std::byte* m_trampoline{};
		std::array<std::byte, kPatchSize> m_original{};
		bool m_armed{};
	};
} // namespace rml::platform::windows
