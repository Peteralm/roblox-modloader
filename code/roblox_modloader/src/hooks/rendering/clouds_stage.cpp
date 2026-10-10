#include "RobloxModLoader/hooking/hooking.hpp"
#include "RobloxModLoader/internal/common.hpp"
#include "RobloxModLoader/internal/hooking/engine_hooks.hpp"
#include "render/engine_stages.hpp"
#include "render/injection_dispatch.hpp"
#include "roblox/graphics/graphics_registry.hpp"

static void composite_stage(const auto& call_original)
{
	using namespace rml::render;
	auto& dispatch = detail::InjectionDispatch::instance();
	const bool inject = dispatch.on_scene_target();
	detail::run_stage(EngineStage::Clouds, inject, call_original);
	if (inject)
		dispatch.reach(InjectionPoint::at(FramePoint::MainAfterOpaque));
}

void rml::Hooks::clouds_update(void* clouds, RBX::Graphics::DeviceContext* context, void* view_info, const RBX::Graphics::RenderCamera* camera, RBX::Graphics::Framebuffer* main_framebuffer, RBX::Graphics::GlobalShaderData* globals, const void* camera_change, void* stats)
{
	render::detail::InjectionDispatch::instance().reach(render::InjectionPoint::at(render::FramePoint::CloudsPrepare));
	if (!render::detail::EngineStages::instance().skips(render::EngineStage::Clouds))
		Hooking::get_original<&Hooks::clouds_update>()(clouds, context, view_info, camera, main_framebuffer, globals, camera_change, stats);
}

void rml::Hooks::clouds_composite(void* clouds, RBX::Graphics::DeviceContext* context, const void* camera, RBX::Graphics::GlobalShaderData* globals, void* stats)
{
	composite_stage([&] { Hooking::get_original<&Hooks::clouds_composite>()(clouds, context, camera, globals, stats); });
}

void rml::Hooks::clouds_composite_clouds(void* clouds, RBX::Graphics::DeviceContext* context, RBX::Graphics::GlobalShaderData* globals, void* stats)
{
	composite_stage([&] { Hooking::get_original<&Hooks::clouds_composite_clouds>()(clouds, context, globals, stats); });
}

void* rml::Hooks::device_destroy(RBX::Graphics::Device* self, const unsigned int flags)
{
	render::detail::InjectionDispatch::instance().device_lost(*self);
	graphics::GraphicsRegistry::instance().on_device_destroyed(self);
	return Hooking::get_original<&Hooks::device_destroy>()(self, flags);
}

void rml::Hooks::device_context_begin_pass(RBX::Graphics::DeviceContext* self, RBX::Graphics::Framebuffer* framebuffer, const unsigned load_mask, unsigned store_mask, const RBX::Graphics::PassClear* clear, const RBX::Graphics::PassResolve* resolve, const unsigned flags)
{
	store_mask = render::detail::InjectionDispatch::instance().suspension().record(framebuffer, store_mask, resolve, flags);
	Hooking::get_original<&Hooks::device_context_begin_pass>()(self, framebuffer, load_mask, store_mask, clear, resolve, flags);
}
