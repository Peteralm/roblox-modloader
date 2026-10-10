#include "mod_marker.hpp"

#include <RobloxModLoader/mod/init_context.hpp>

bool ModMarker::ask_set_parent(const RBX::Instance* parent) const
{
	return parent && parent->get_descriptor().is_a("Workspace");
}

void ModMarker::define(rml::InitContext& context)
{
	context.define_class<ModMarker>("ModMarker")
	    .description("Only accepts Workspace as its parent (an ask_set_parent override).")
	    .insert_category("RML")
	    .preferred_parent("Workspace")
	    .icon_of("Configuration")
	    .property("Tag", &ModMarker::tag).description("Free-form tag.")
	    .commit();
}
