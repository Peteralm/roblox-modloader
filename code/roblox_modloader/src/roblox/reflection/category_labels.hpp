#pragma once

#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace rml::reflection
{
	class CategoryLabels
	{
	public:
		static CategoryLabels& instance();

		void add(std::string_view name);
		[[nodiscard]] std::optional<std::string> find(std::string_view translation_suffix) const;

	private:
		mutable std::mutex m_mutex;
		std::unordered_map<std::string, std::string> m_names;
	};
}
