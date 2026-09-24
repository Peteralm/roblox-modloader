#pragma once

#include <expected>
#include <string>

namespace rml::reflection
{
	/// Resolves, once, every engine entry point class registration calls and proves the layout facts it
	/// writes through, reaching them through RTTI from the engine's own registration of Folder. The
	/// error names the first step that failed.
	[[nodiscard]] const std::expected<void, std::string>& resolve_engine_anchors();
}
