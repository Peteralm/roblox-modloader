#pragma once

#include "RobloxModLoader/roblox/graphics/render_pass.hpp"
#include "RobloxModLoader/roblox/graphics/visual_engine.hpp"

#include <atomic>
#include <mutex>
#include <vector>

namespace rml::graphics
{
	class GraphicsRegistry
	{
	public:
		static GraphicsRegistry& instance();

		[[nodiscard]] RBX::Graphics::VisualEngine* visual_engine() const;
		[[nodiscard]] RBX::Graphics::Device* device() const;
		void set_visual_engine(RBX::Graphics::VisualEngine* engine);
		void add_render_callback(RenderCallback callback);
		void run_render_callbacks(RenderPassContext& context);
		[[nodiscard]] bool validate();

	private:
		struct Entry
		{
			RenderCallback callback;
			unsigned failures;
		};

		std::atomic<RBX::Graphics::VisualEngine*> m_visual_engine{nullptr};
		std::mutex m_callbacks_mutex;
		std::vector<Entry> m_callbacks;
		std::atomic<int> m_validation{0};
	};
}
