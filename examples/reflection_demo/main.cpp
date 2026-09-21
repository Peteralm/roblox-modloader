#include <RobloxModLoader/logger/logger.hpp>
#include <RobloxModLoader/mod/init_context.hpp>
#include <RobloxModLoader/mod/mod_base.hpp>
#include <RobloxModLoader/roblox/reflection/class_builder.hpp>
#include <spdlog/spdlog.h>

class reflection_demo final : public ModBase
{
	std::shared_ptr<spdlog::logger> m_log;

public:
	reflection_demo()
	{
		name = "Reflection Demo";
		version = "0.1.0";
		author = "RML";
		description = "Registers ModThing through on_init";
		m_log = rml::Logger::get_logger("ReflectionDemo");
	}

	void on_load() override
	{
		m_log->info("loaded");
	}

	void on_init(rml::InitContext& context) override
	{
		context.define_class("ModThing").commit();
		m_log->info("ModThing registered");
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
