#pragma once

#include "RobloxModLoader/rml_export.hpp"

#include <functional>

namespace RBX
{
	class Adorn;
}

namespace RBX::Graphics
{
	class Device;
	class DeviceContext;
	class Framebuffer;
	class AdornRender;
	class RenderCamera;
	class SceneManager;
	class VisualEngine;
}

namespace rml::graphics
{
	struct RenderPassContext
	{
		RBX::Graphics::DeviceContext* context;
		RBX::Graphics::Framebuffer* target;
		RBX::Graphics::Device* device;
		const RBX::Graphics::RenderCamera* camera;
		RBX::Graphics::SceneManager* scene_manager;
	};

	using RenderCallback = std::function<void(RenderPassContext&)>;
	using AdornCallback = std::function<void(RBX::Adorn&)>;

	RML_EXPORT RBX::Graphics::VisualEngine* visual_engine();
	RML_EXPORT RBX::Graphics::Device* device();
	RML_EXPORT RBX::Graphics::SceneManager* scene_manager();
	RML_EXPORT void add_render_callback(RenderCallback callback);
	RML_EXPORT void add_adorn_callback(AdornCallback callback);
	RML_EXPORT RBX::Graphics::AdornRender* adorn_render();
}
