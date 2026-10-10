#include "mod_marker.hpp"
#include "mod_thing.hpp"
#include "thing_enums.hpp"
#include "workspace_extension.hpp"

#include <RobloxModLoader/logger/logger.hpp>
#include <RobloxModLoader/mod/init_context.hpp>
#include <RobloxModLoader/mod/mod_base.hpp>
#include <spdlog/spdlog.h>

class reflection_demo final : public ModBase
{
public:
	reflection_demo()
	{
		name = "Reflection Demo";
		version = "0.2.0";
		author = "RML";
		description = "Registers ModThing, ModMarker and a Workspace extension through on_init";
	}

	void on_load() override
	{
	}

	void on_unload() override
	{
	}

	void on_init(rml::InitContext& context) override
	{
		define_thing_enums(context);
		ModThing::define(context);
		ModMarker::define(context);
		extend_workspace(context);
		rml::Logger::get_logger("ReflectionDemo")->info("reflection demo registered ({} bytes per ModThing)", sizeof(ModThing));
	}
};

extern "C"
{
	RML_MOD_ABI_EXPORT ModBase* start_mod()
	{
		return new reflection_demo();
	}

	RML_MOD_ABI_EXPORT void uninstall_mod(const ModBase* mod)
	{
		delete mod;
	}
}

RML_EXPORT_MOD_ABI_VERSION()
