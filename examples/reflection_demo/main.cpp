#include <RobloxModLoader/logger/logger.hpp>
#include <RobloxModLoader/mod/init_context.hpp>
#include <RobloxModLoader/mod/mod_base.hpp>
#include <RobloxModLoader/roblox/reflection/described_creatable.hpp>
#include <lua.h>
#include <spdlog/spdlog.h>

#include <string>

static std::shared_ptr<spdlog::logger> g_log;

class ModThing final : public rml::reflection::DescribedCreatable<ModThing>
{
public:
	float speed{};
	std::string label;

	int reset(lua_State* L)
	{
		const auto previous = speed;
		speed = 0.f;
		label = "reset";
		lua_pushnumber(L, previous);
		return 1;
	}

	void on_child_added(RBX::Instance* child) override
	{
		g_log->info("on_child_added: {} <- {}", label, child->name.value());
	}
};

class reflection_demo final : public ModBase
{
public:
	reflection_demo()
	{
		name = "Reflection Demo";
		version = "0.1.0";
		author = "RML";
		description = "Registers ModThing through on_init";
		g_log = rml::Logger::get_logger("ReflectionDemo");
	}

	void on_load() override
	{
		g_log->info("loaded");
	}

	void on_init(rml::InitContext& context) override
	{
		context.define_class<ModThing>("ModThing")
		    .property("Speed", &ModThing::speed)
		    .property("Label", &ModThing::label)
		    .function("Reset", &ModThing::reset)
		    .commit();
		g_log->info("ModThing registered ({} bytes)", sizeof(ModThing));
	}

	void on_unload() override
	{
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
