#include "RobloxModLoader/hooking/hooking.hpp"
#include "RobloxModLoader/internal/common.hpp"
#include "RobloxModLoader/internal/hooking/engine_hooks.hpp"
#include "roblox/graphics/graphics_registry.hpp"

void rml::Hooks::scene_manager_render_scene(void* self, RBX::Graphics::DeviceContext* context, RBX::Graphics::Framebuffer* target, const void* camera,
    RBX::ArrayView<RBX::Graphics::Framebuffer*> extra, std::uint32_t capture_mode)
{
	Hooking::get_original<&Hooks::scene_manager_render_scene>()(self, context, target, camera, extra, capture_mode);

	auto& registry = graphics::GraphicsRegistry::instance();
	if (!registry.validate())
		return;

	graphics::RenderPassContext pass{context, target, registry.device()};
	registry.run_render_callbacks(pass);
}
