#include "graphics_registry.hpp"

#include "RobloxModLoader/hooking/vtable_index.hpp"
#include "RobloxModLoader/internal/common.hpp"
#include "RobloxModLoader/memory/module.hpp"
#include "RobloxModLoader/platform/memory/host_image.hpp"
#include "RobloxModLoader/roblox/graphics/device.hpp"

#include <algorithm>
#include <cctype>

RML_LOG_SCOPE("Graphics");

namespace rml::graphics
{
	static constexpr unsigned k_max_callback_failures = 2;

	static bool printable(const std::string& text)
	{
		return !text.empty() && text.size() < 64 && std::all_of(text.begin(), text.end(), [](const unsigned char c) { return std::isprint(c); });
	}

	GraphicsRegistry& GraphicsRegistry::instance()
	{
		static GraphicsRegistry registry;
		return registry;
	}

	RBX::Graphics::VisualEngine* GraphicsRegistry::visual_engine() const
	{
		return m_visual_engine.load(std::memory_order_acquire);
	}

	RBX::Graphics::Device* GraphicsRegistry::device() const
	{
		const auto engine = visual_engine();
		return engine ? engine->device : nullptr;
	}

	void GraphicsRegistry::set_visual_engine(RBX::Graphics::VisualEngine* engine)
	{
		if (m_visual_engine.exchange(engine, std::memory_order_acq_rel) != engine)
			RML_INFO("VisualEngine captured at 0x{:X} (device 0x{:X})", reinterpret_cast<std::uintptr_t>(engine), reinterpret_cast<std::uintptr_t>(engine ? engine->device : nullptr));
	}

	void GraphicsRegistry::add_render_callback(RenderCallback callback)
	{
		std::lock_guard lock(m_callbacks_mutex);
		m_callbacks.push_back({std::move(callback), 0});
	}

	void GraphicsRegistry::run_render_callbacks(RenderPassContext& context)
	{
		std::lock_guard lock(m_callbacks_mutex);
		for (auto it = m_callbacks.begin(); it != m_callbacks.end();)
		{
			try
			{
				it->callback(context);
				++it;
				continue;
			}
			catch (const std::exception& e)
			{
				RML_ERROR("render callback threw: {}", e.what());
			}
			catch (...)
			{
				RML_ERROR("render callback threw an unknown exception");
			}

			if (++it->failures >= k_max_callback_failures)
			{
				RML_ERROR("render callback removed after {} failures", it->failures);
				it = m_callbacks.erase(it);
			}
			else
			{
				++it;
			}
		}
	}

	bool GraphicsRegistry::validate()
	{
		if (const auto state = m_validation.load(std::memory_order_acquire); state != 0)
			return state > 0;

		const auto check = [&]() -> bool {
			auto* device = this->device();
			if (!device)
			{
				RML_ERROR("graphics surface disabled: VisualEngine has no device");
				return false;
			}

			const memory::module image(platform::studio_image_name());
			auto** vtable = *reinterpret_cast<void***>(device);
			const auto slots = vtable_index_of(&RBX::Graphics::Device::create_texture_with_hardware_buffer_impl, RBX::Graphics::Texture::Type::Type_2D,
			                       RBX::Graphics::Texture::Format::RGBA8, 0u, 0u, 0u, 0u, 0u, 0u, RBX::Graphics::Texture::Usage::Static, std::string{}, nullptr)
			                   + 1;
			for (std::size_t slot = 0; slot < slots; ++slot)
			{
				if (!image.contains(memory::handle(vtable[slot])))
				{
					RML_ERROR("graphics surface disabled: Device vtable slot {} is not code", slot);
					return false;
				}
			}
			if (image.contains(memory::handle(vtable[slots])))
			{
				RML_ERROR("graphics surface disabled: Device vtable has more than {} slots", slots);
				return false;
			}

			const auto language = device->get_shading_language();
			const auto level = device->get_feature_level();
			if (!printable(language) || !printable(level))
			{
				RML_ERROR("graphics surface disabled: Device strings are not printable");
				return false;
			}

			RML_INFO("Device: shading language '{}', feature level '{}', {} vtable slots", language, level, slots);
			return true;
		};

		const bool ok = check();
		m_validation.store(ok ? 1 : -1, std::memory_order_release);
		return ok;
	}

	RBX::Graphics::VisualEngine* visual_engine()
	{
		return GraphicsRegistry::instance().visual_engine();
	}

	RBX::Graphics::Device* device()
	{
		return GraphicsRegistry::instance().device();
	}

	void add_render_callback(RenderCallback callback)
	{
		GraphicsRegistry::instance().add_render_callback(std::move(callback));
	}
}
