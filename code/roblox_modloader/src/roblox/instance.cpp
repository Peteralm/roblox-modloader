#include "RobloxModLoader/roblox/instance.hpp"

#include "RobloxModLoader/internal/common.hpp"

namespace RBX
{
	std::string Instance::compute_full_name()
	{
		std::string full_name{name.value()};
		for (Instance* ancestor = parent; ancestor != nullptr; ancestor = ancestor->parent)
			full_name = ancestor->name.value() + "." + full_name;
		return full_name;
	}

	std::size_t Instance::num_children() const
	{
		return children ? children->size() : 0;
	}

	Instance* Instance::get_child(const std::size_t index)
	{
		return (*children)[index].get();
	}

	const Instance* Instance::get_child(const std::size_t index) const
	{
		return (*children)[index].get();
	}

	const Instance* Instance::find_first_child_by_name(const std::string_view find_name) const
	{
		if (!children)
			return nullptr;
		const auto& c = *children;
		for (std::size_t i = 0; i < c.size(); ++i)
		{
			if (c[i]->name.value() == find_name)
				return c[i].get();
		}
		return nullptr;
	}

	Instance* Instance::find_first_child_by_name(const std::string_view find_name)
	{
		return const_cast<Instance*>(std::as_const(*this).find_first_child_by_name(find_name));
	}

	Instance* Instance::find_first_child_of_type(const std::string_view class_name)
	{
		return const_cast<Instance*>(std::as_const(*this).find_first_child_of_type(class_name));
	}

	const Instance* Instance::find_first_child_of_type(const std::string_view class_name) const
	{
		if (children)
		{
			for (const auto& child : *children)
			{
				if (child->get_descriptor().name.to_string() == class_name)
					return child.get();
			}
		}
		return nullptr;
	}
}
