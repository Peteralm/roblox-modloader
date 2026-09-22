#include "RobloxModLoader/hooking/hooking.hpp"
#include "RobloxModLoader/internal/common.hpp"
#include "RobloxModLoader/internal/hooking/engine_hooks.hpp"
#include "RobloxModLoader/roblox/graphics/adorn_render.hpp"
#include "roblox/graphics/graphics_registry.hpp"

void rml::Hooks::adorn_render_pre_submit_pass(RBX::Graphics::AdornRender* self)
{
	Hooking::get_original<&Hooks::adorn_render_pre_submit_pass>()(self);
	graphics::GraphicsRegistry::instance().run_adorn_callbacks(*self);
}
