#include "mod_thing.hpp"

#include <RobloxModLoader/logger/logger.hpp>
#include <RobloxModLoader/mod/init_context.hpp>
#include <lua.h>
#include <spdlog/spdlog.h>

int ModThing::reset(lua_State* L)
{
	const auto previous = speed;
	speed = 0.f;
	label = "reset";
	fire(&ModThing::speed_reset, previous);
	lua_pushnumber(L, previous);
	return 1;
}

void ModThing::on_child_added(RBX::Instance* child)
{
	rml::Logger::get_logger("ReflectionDemo")->info("on_child_added: {} <- {}", label, child->name.value());
}

void ModThing::define(rml::InitContext& context)
{
	using rml::reflection::SliderScaling;
	context.define_class<ModThing>("ModThing")
	    .description("Example class from reflection_demo. It shows every Properties panel option the loader supports.")
	    .insert_category("RML")
	    .explorer_order(1)
	    .preferred_parent("Workspace")
	    .icon_of("Folder")
	    .property("Enabled", &ModThing::enabled).category("Behavior").order(0)
	    .property("Speed", &ModThing::speed).category("Motion").order(10).slider(0.f, 100.f, 100).description("Units per second. A linear slider with 100 steps.")
	    .property("Volume", &ModThing::volume).category("Motion").order(11).slider(0.0, 10.0, 1000, SliderScaling::Square).description("A square-scaled slider, like Sound.Volume.")
	    .property("Count", &ModThing::count).category("Motion").order(12).slider(0, 20, 20)
	    .property("Tint", &ModThing::tint).category("Appearance").order(20)
	    .property("Offset", &ModThing::offset).category("Appearance").order(21)
	    .property("Label", &ModThing::label).category("Data").order(30).read_only().description("Read-only in the Properties panel; scripts can still write it.")
	    .property("Notes", &ModThing::notes).category("Data").order(31)
	    .property("Mode", &ModThing::mode).category("Behavior").order(1).description("A mod enum; the field starts at 0, which is not an item, and reads as Idle.")
	    .property("Face", &ModThing::face).category("Behavior").order(2).description("Bound to the engine's NormalId.")
	    .property("Legacy", &ModThing::legacy).category("Data").order(32).deprecated("Use Speed instead.")
	    .property("Debug", &ModThing::debug).hidden()
	    .function("Reset", &ModThing::reset)
	    .event("SpeedReset", &ModThing::speed_reset, {"previous"})
	    .commit();
}
