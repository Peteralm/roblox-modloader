#include "RobloxModLoader/internal/common.hpp"
#include "RobloxModLoader/memory/i_rtti_provider.hpp"
#include "RobloxModLoader/memory/module.hpp"
#include "RobloxModLoader/platform/memory/host_image.hpp"

#include <string>
#include <vector>

RML_LOG_SCOPE("ItaniumRtti");

namespace rml::memory
{
	static std::string itanium_substitution(const std::size_t index)
	{
		return index == 0 ? "S_" : "S" + std::to_string(index - 1) + "_";
	}

	static std::string itanium_template_argument(const std::string_view argument, const std::size_t substitutions)
	{
		if (argument == "bool")
			return "b";
		if (argument == "int")
			return "i";
		if (argument == "float")
			return "f";
		if (argument == "double")
			return "d";
		if (argument == "std::string")
		{
			const auto std_namespace = itanium_substitution(substitutions);
			return "NSt3__112basic_stringIcN" + std_namespace + "11char_traitsIcEEN" + std_namespace + "9allocatorIcEEEE";
		}
		return {};
	}

	static std::string itanium_type_name(const std::string_view class_name)
	{
		const auto template_open = class_name.find('<');
		const auto qualified = class_name.substr(0, template_open);

		std::vector<std::string_view> components;
		for (std::size_t start = 0;;)
		{
			const auto separator = qualified.find("::", start);
			const auto end = separator == std::string_view::npos ? qualified.size() : separator;

			components.push_back(qualified.substr(start, end - start));

			if (separator == std::string_view::npos)
				break;

			start = separator + 2;
		}

		std::string name;
		for (const auto component : components)
		{
			name += std::to_string(component.size());
			name += component;
		}

		if (template_open != std::string_view::npos)
		{
			const auto argument = class_name.substr(template_open + 1, class_name.rfind('>') - template_open - 1);
			name += "I" + itanium_template_argument(argument, components.size()) + "E";
		}

		return components.size() > 1 ? "N" + name + "E" : name;
	}

	static const char* find_type_name_string(const memory::module& image, const std::string_view type_name)
	{
		const std::string_view haystack{image.begin().as<const char*>(), image.size()};

		std::string needle{type_name};
		needle.push_back('\0');

		const auto position = haystack.find(needle);
		return position == std::string_view::npos ? nullptr : haystack.data() + position;
	}

	static std::vector<const std::uintptr_t*> find_references(const memory::module& image, const void* target)
	{
		const auto* const slots = image.begin().as<const std::uintptr_t*>();
		const std::size_t count = image.size() / sizeof(std::uintptr_t);
		const auto value = reinterpret_cast<std::uintptr_t>(target);

		std::vector<const std::uintptr_t*> references;
		for (std::size_t i = 0; i < count; ++i)
		{
			if (slots[i] == value)
				references.push_back(slots + i);
		}

		return references;
	}

	static std::optional<void**> vtable_for_name_string(const memory::module& image, const char* name_string)
	{
		for (const auto* const name_reference : find_references(image, name_string))
		{
			const auto* const type_info = name_reference - 1;

			for (const auto* const type_info_reference : find_references(image, type_info))
			{
				if (*(type_info_reference - 1) != 0)
					continue;

				return reinterpret_cast<void**>(const_cast<std::uintptr_t*>(type_info_reference + 1));
			}
		}

		return std::nullopt;
	}

	class ItaniumRttiProvider final : public IRttiProvider
	{
	public:
		std::optional<void**> find_class_vtable(const std::string_view class_name) override
		{
			const module image{platform::studio_image_name()};
			if (!image.loaded())
				return std::nullopt;

			const std::string type_name = itanium_type_name(class_name);

			const char* const name_string = find_type_name_string(image, type_name);
			if (!name_string)
			{
				RML_WARN("No RTTI name '{}' in the image for class '{}'", type_name, class_name);
				return std::nullopt;
			}

			const auto vtable = vtable_for_name_string(image, name_string);
			if (vtable)
				RML_DEBUG("RTTI '{}' -> vtable {}", class_name, static_cast<void*>(*vtable));
			else
				RML_WARN("Found the RTTI name for '{}' but no primary vtable pointing at it", class_name);
			return vtable;
		}

		std::optional<void**> find_class_vtable_matching(const std::string_view mangled_prefix, const std::function<bool(std::string_view)>& accept) override
		{
			const module image{platform::studio_image_name()};
			if (!image.loaded())
				return std::nullopt;

			const std::string_view haystack{image.begin().as<const char*>(), image.size()};
			std::string needle;
			needle.push_back('\0');
			needle.append(mangled_prefix);

			for (auto position = haystack.find(needle); position != std::string_view::npos; position = haystack.find(needle, position + 1))
			{
				const char* const name_string = haystack.data() + position + 1;
				const std::string_view name{name_string};
				if (!accept(name))
					continue;

				if (const auto vtable = vtable_for_name_string(image, name_string))
				{
					RML_DEBUG("RTTI '{}' -> vtable {}", name, static_cast<void*>(*vtable));
					return vtable;
				}
			}

			return std::nullopt;
		}
	};

	std::unique_ptr<IRttiProvider> create_rtti_provider()
	{
		return std::make_unique<ItaniumRttiProvider>();
	}
}
