#pragma once

#include "RobloxModLoader/rml_export.hpp"

#include <memory>
#include <string_view>

namespace rml::reflection
{
	struct ClassSpec;

	class RML_EXPORT ClassBuilder
	{
	public:
		ClassBuilder(std::string_view name, std::string_view base);
		~ClassBuilder();
		ClassBuilder(ClassBuilder&&) noexcept;
		ClassBuilder& operator=(ClassBuilder&&) noexcept;

		ClassBuilder& base(std::string_view engine_class);
		void commit();

	private:
		std::unique_ptr<ClassSpec> m_spec;
	};
}
