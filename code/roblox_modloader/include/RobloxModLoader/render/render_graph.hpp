#pragma once

#include "RobloxModLoader/render/render_pass.hpp"
#include "RobloxModLoader/rml_export.hpp"

#include <memory>
#include <string_view>
#include <vector>

namespace rml::render
{
	class RenderGraph
	{
	public:
		virtual ~RenderGraph() = default;

		virtual void add_pass(std::unique_ptr<IRenderPass> pass) = 0;
		virtual void remove_pass(std::string_view name) = 0;
		[[nodiscard]] virtual IRenderPass* find_pass(std::string_view name) const = 0;
		[[nodiscard]] virtual std::vector<IRenderPass*> passes() const = 0;

		template<typename T>
		[[nodiscard]] T* find_pass_of_type() const
		{
			for (auto* pass : passes())
			{
				if (auto* typed = dynamic_cast<T*>(pass))
					return typed;
			}
			return nullptr;
		}
	};

	[[nodiscard]] RML_EXPORT RenderGraph& graph();
}
