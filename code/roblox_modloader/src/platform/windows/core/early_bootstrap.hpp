#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>

namespace rml::platform::windows
{
	enum class BootstrapState : long { Unarmed, Armed, Running, Completed, Failed };
	enum class ResolveError { InvalidImage, UnknownBuild, MissingSignature, AmbiguousSignature, InvalidShape };

	struct EmbeddedBootstrapProfile
	{
		std::uint32_t pe_timestamp;
		std::uint32_t size_of_image;
		const char* build;
		std::uint32_t window_rva;
		std::uint32_t registry_vector_rva;
		std::uint32_t registry_frozen_rva;
		std::uint32_t class_count_rva;
		std::uint32_t engine_allocate_rva;
		std::uint32_t engine_free_rva;
		std::uint8_t overwrite_size;
		const std::uint8_t* signature;
		std::size_t signature_size;
	};

	struct ResolvedGlobalInitWindow
	{
		std::byte* host;
		std::byte* target;
		const EmbeddedBootstrapProfile* profile;
	};

	[[nodiscard]] std::expected<ResolvedGlobalInitWindow, ResolveError> resolve_global_init_window(
	    void* host_module, std::span<const EmbeddedBootstrapProfile> profiles) noexcept;

	class EarlyBootstrap
	{
	public:
		static void arm(void* loader_module) noexcept;
		[[nodiscard]] static BootstrapState state() noexcept;
		[[nodiscard]] static const char* diagnostic() noexcept;
		static void invoke() noexcept;
	};
} // namespace rml::platform::windows
