#include "thing_enums.hpp"

#include <RobloxModLoader/mod/init_context.hpp>

void define_thing_enums(rml::InitContext& context)
{
	context.define_enum<ThingMode>("ThingMode")
	    .description("What a ModThing is doing. A mod-defined enum.")
	    .item("Idle", ThingMode::Idle)
	    .item("Patrol", ThingMode::Patrol)
	    .item("Chase", ThingMode::Chase)
	    .item("Flee", ThingMode::Flee)
	    .item("Legacy", ThingMode::Legacy)
	    .commit();

	context.bind_enum<Face>("NormalId")
	    .item("Right", Face::Right)
	    .item("Top", Face::Top)
	    .item("Back", Face::Back)
	    .item("Left", Face::Left)
	    .item("Bottom", Face::Bottom)
	    .item("Front", Face::Front)
	    .commit();
}
