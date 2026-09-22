#pragma once

#include "RobloxModLoader/roblox/job.hpp"
#include "RobloxModLoader/roblox/reflection/array_view.hpp"
#include "RobloxModLoader/roblox/task_scheduler.hpp"

#include <cstdint>
#include <lua.h>

namespace RBX
{
	class TaskSchedulerJob;
	class ICreator;
	class Name;

	namespace Graphics
	{
		class AdornRender;
		class DeviceContext;
		class Framebuffer;
		class VisualEngine;
	}
}

namespace rml
{
	struct Hooks
	{
		static void rbx_crash(const char* type, const char* message);
		static uint64_t* on_authentication(uint64_t* _this, uint64_t doc_panel_provider, uint64_t q_image_provider);
		static std::uintptr_t* build_summary(uintptr_t* _this, std::uintptr_t* out);
		static RBX::TaskScheduler::StepResult on_job_step(void** this_ptr, const RBX::Stats& time_metrics);
		static void on_job_destroy(void** this_ptr);
		static void resume_waiting_scripts(uintptr_t* script_context, int expiration_time);
		static void light_grid_update_perform(void* this_ptr, uintptr_t unk, void* unk2, uintptr_t unk3);
		static uintptr_t profile_log(uintptr_t token, uint64_t tick, uint64_t begin, uintptr_t* log);
		static lua_Status luau_load(lua_State* L, const char* chunkname, const char* data, size_t size, int env);
		static void* build_menu_bar_from_dom(void* out_menu_bar, void* dom, void* context);
		static void qt_action_activate(void* self, int event);
		static void global_init();
		static const RBX::ICreator* creatable_get_creator(const RBX::Name* name);
		static RBX::Graphics::DeviceContext* visual_engine_begin_render(RBX::Graphics::VisualEngine* self);
		static void adorn_render_pre_submit_pass(RBX::Graphics::AdornRender* self);
		static void scene_manager_render_scene(void* self, RBX::Graphics::DeviceContext* context, RBX::Graphics::Framebuffer* target, const void* camera, RBX::ArrayView<RBX::Graphics::Framebuffer*> extra, std::uint32_t capture_mode);
	};
}