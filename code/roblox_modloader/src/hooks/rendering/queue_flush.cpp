#include "RobloxModLoader/hooking/hooking.hpp"
#include "RobloxModLoader/internal/common.hpp"
#include "RobloxModLoader/internal/hooking/engine_hooks.hpp"
#include "render/injection_dispatch.hpp"
#include "render/injection_table.hpp"
#include "roblox/graphics/graphics_registry.hpp"

static void queue_flush(const std::optional<rml::render::QueueGroup> group, const auto& call_original)
{
	if (!group)
	{
		call_original();
		return;
	}
	auto& dispatch = rml::render::detail::InjectionDispatch::instance();
	rml::render::detail::run_queue(*group, dispatch.on_scene_target() || dispatch.on_output_target(), call_original);
}

void rml::Hooks::render_objects_clipped(RBX::Graphics::DeviceContext* context, void* instance_glob, const void* view, void* group, void* stats, const std::uint32_t tracker, const void* tokens, void* clip, const bool first, const bool second)
{
	const auto* scene = graphics::GraphicsRegistry::instance().scene_manager();
	const auto resolved = scene ? render::detail::InjectionTable::instance().group_of(*scene, group) : std::nullopt;
	queue_flush(resolved, [&] { Hooking::get_original<&Hooks::render_objects_clipped>()(context, instance_glob, view, group, stats, tracker, tokens, clip, first, second); });
}

void rml::Hooks::dispatch_scene_dispatch(void* self, const void* context, const void* view, const std::uint32_t pass, const std::uint32_t id, const void* query)
{
	render::detail::InjectionDispatch::instance().note_main_view();
	queue_flush(render::detail::InjectionTable::instance().group_of(id), [&] { Hooking::get_original<&Hooks::dispatch_scene_dispatch>()(self, context, view, pass, id, query); });
}
