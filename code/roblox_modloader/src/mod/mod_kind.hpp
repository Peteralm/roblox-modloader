#pragma once

#include "RobloxModLoader/internal/platform.hpp"

#include <array>
#include <optional>
#include <string_view>

namespace rml
{
	enum class ModKind
	{
		Native,
		Dotnet,
		Scripts
	};

	struct ModKindFolder
	{
		ModKind kind;
		std::string_view folder_name;
	};

	inline constexpr std::array<ModKindFolder, 3> kModKindFolders{{
	    {ModKind::Native, "native"},
	    {ModKind::Dotnet, "dotnet"},
	    {ModKind::Scripts, "scripts"},
	}};

#if defined(RML_WINDOWS)
	inline constexpr std::array<std::string_view, 1> kNativeModExtensions{".dll"};
#elif defined(RML_MACOS)
	inline constexpr std::array<std::string_view, 2> kNativeModExtensions{".dylib", ".so"};
#else
	inline constexpr std::array<std::string_view, 1> kNativeModExtensions{".so"};
#endif

	[[nodiscard]] std::optional<ModKind> mod_kind_from_folder(std::string_view folder_name) noexcept;
	[[nodiscard]] std::string_view mod_kind_folder_name(ModKind kind) noexcept;
}
