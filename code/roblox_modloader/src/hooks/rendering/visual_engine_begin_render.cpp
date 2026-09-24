#include "RobloxModLoader/hooking/hooking.hpp"
#include "RobloxModLoader/internal/common.hpp"
#include "RobloxModLoader/internal/hooking/engine_hooks.hpp"
#include "roblox/graphics/graphics_registry.hpp"

RBX::Graphics::DeviceContext* rml::Hooks::visual_engine_begin_render(RBX::Graphics::VisualEngine* self)
{
	graphics::GraphicsRegistry::instance().set_visual_engine(self);
	return Hooking::get_original<&Hooks::visual_engine_begin_render>()(self);
}
