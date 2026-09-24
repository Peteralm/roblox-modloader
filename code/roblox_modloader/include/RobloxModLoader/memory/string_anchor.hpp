#pragma once

#include "RobloxModLoader/rml_export.hpp"

#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

namespace rml::memory
{
	struct AnchoredFunction
	{
		void* start;
		std::size_t size;
	};

	RML_EXPORT std::vector<AnchoredFunction> functions_referencing_string(std::string_view exact_text);
	RML_EXPORT std::vector<AnchoredFunction> functions_calling(const void* target);
	RML_EXPORT std::optional<AnchoredFunction> function_containing(const void* address);
}
