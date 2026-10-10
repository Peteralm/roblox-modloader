#include "workspace_extension.hpp"

#include <RobloxModLoader/mod/init_context.hpp>
#include <lua.h>

static int g_workspace_counter;

static int workspace_ping(RBX::Instance*, lua_State* L)
{
	lua_pushstring(L, "pong");
	return 1;
}

static int get_workspace_counter(RBX::Instance*)
{
	return g_workspace_counter;
}

static void set_workspace_counter(RBX::Instance*, const int& value)
{
	g_workspace_counter = value;
}

void extend_workspace(rml::InitContext& context)
{
	context.extend_class("Workspace")
	    .function("RmlPing", &workspace_ping)
	    .property("RmlCounter", &get_workspace_counter, &set_workspace_counter).category("RML").slider(0, 10, 10).description("Counter added to Workspace by reflection_demo.")
	    .commit();
}
