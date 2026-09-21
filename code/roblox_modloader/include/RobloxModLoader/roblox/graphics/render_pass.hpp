#pragma once

#include "RobloxModLoader/rml_export.hpp"

#include <functional>

namespace RBX::Graphics
{
	class Device;
	class DeviceContext;
	class Framebuffer;
	class RenderCamera;
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
		void* scene_manager;
	};

	using RenderCallback = std::function<void(RenderPassContext&)>;

	RML_EXPORT RBX::Graphics::VisualEngine* visual_engine();
	RML_EXPORT RBX::Graphics::Device* device();
	RML_EXPORT void add_render_callback(RenderCallback callback);
}
