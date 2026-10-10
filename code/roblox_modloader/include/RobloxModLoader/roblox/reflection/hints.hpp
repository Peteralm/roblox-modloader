#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace rml::reflection
{
	enum class SliderScaling : std::uint8_t
	{
		Linear,
		Square
	};

	struct Slider
	{
		double min{};
		double max{};
		int ticks{};
		SliderScaling scaling{SliderScaling::Linear};
	};

	struct PropertyHints
	{
		std::optional<std::string> description;
		std::optional<int> order;
		bool read_only{};
		bool hidden{};
		std::optional<std::string> deprecated;
		std::optional<Slider> slider;

		[[nodiscard]] bool empty() const
		{
			return !description && !order && !read_only && !hidden && !deprecated && !slider;
		}
	};

	struct ClassHints
	{
		std::optional<std::string> description;
		std::optional<std::string> insert_category;
		std::optional<std::string> preferred_parent;
		std::optional<std::string> icon_of;
		std::optional<int> explorer_order;
		std::optional<bool> insertable;
		std::optional<bool> browsable;

		[[nodiscard]] bool empty() const
		{
			return !description && !insert_category && !preferred_parent && !icon_of && !explorer_order && !insertable && !browsable;
		}
	};

	struct EnumHints
	{
		std::optional<std::string> description;
	};

	struct EnumItemHints
	{
		std::optional<std::string> description;
		bool hidden{};
		std::optional<std::string> deprecated;

		[[nodiscard]] bool empty() const
		{
			return !description && !hidden && !deprecated;
		}
	};
}
