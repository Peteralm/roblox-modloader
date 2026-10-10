#include "category_labels.hpp"

#include <cctype>

namespace rml::reflection
{
	static std::string translation_suffix_of(const std::string_view name)
	{
		std::string suffix;
		for (const auto c : name)
		{
			if (std::isalnum(static_cast<unsigned char>(c)))
				suffix.push_back(c);
		}
		return suffix;
	}

	CategoryLabels& CategoryLabels::instance()
	{
		static CategoryLabels labels;
		return labels;
	}

	void CategoryLabels::add(const std::string_view name)
	{
		auto suffix = translation_suffix_of(name);
		if (suffix.empty())
			return;
		std::lock_guard lock(m_mutex);
		m_names.try_emplace(std::move(suffix), name);
	}

	std::optional<std::string> CategoryLabels::find(const std::string_view translation_suffix) const
	{
		std::lock_guard lock(m_mutex);
		const auto it = m_names.find(std::string(translation_suffix));
		return it == m_names.end() ? std::nullopt : std::optional(it->second);
	}
}
