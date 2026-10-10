#include "RobloxModLoader/hooking/hooking.hpp"
#include "RobloxModLoader/internal/common.hpp"
#include "RobloxModLoader/internal/hooking/engine_hooks.hpp"
#include "RobloxModLoader/qt/qstring.hpp"
#include "roblox/reflection/category_labels.hpp"

#include <array>
#include <string_view>

static constexpr std::array<std::string_view, 2> k_category_prefixes{"Studio.App.InsertObjectCategory.", "Studio.RobloxPropertiesWidget.Category."};

rml::qt::QString rml::Hooks::qt_translate(const char* context, const char* key, const char* disambiguation, const int n)
{
	const auto original = Hooking::get_original<&Hooks::qt_translate>();
	if (!key)
		return original(context, key, disambiguation, n);

	try
	{
		const std::string_view text(key);
		for (const auto prefix : k_category_prefixes)
		{
			if (!text.starts_with(prefix))
				continue;
			const auto name = reflection::CategoryLabels::instance().find(text.substr(prefix.size()));
			if (!name)
				break;
			auto translated = original(context, key, disambiguation, n);
			if (translated.to_utf8() != text)
				return translated;
			return qt::QString(*name);
		}
	}
	catch (...)
	{
	}
	return original(context, key, disambiguation, n);
}
