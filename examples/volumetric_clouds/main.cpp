#include "cloud_passes.hpp"
#include "cloud_state.hpp"
#include "volumetric_clouds.hpp"

#include <RobloxModLoader/logger/logger.hpp>
#include <RobloxModLoader/mod/init_context.hpp>
#include <RobloxModLoader/mod/mod_base.hpp>
#include <RobloxModLoader/render/render_graph.hpp>
#include <spdlog/spdlog.h>

#include <memory>

class VolumetricCloudsMod final : public ModBase
{
public:
	VolumetricCloudsMod()
	{
		name = "Volumetric Clouds";
		version = "1.1.0";
		author = "Revolution";
		description = "Adds the VolumetricClouds instance";
		m_log = rml::Logger::get_logger("VolumetricClouds");
	}

	void on_init(rml::InitContext& context) override
	{
		clouds::VolumetricClouds::define(context);
		m_log->info("VolumetricClouds registered");
	}

	void on_load() override
	{
		auto state = std::make_shared<clouds::CloudState>(paths().dir("cache") / "noise.bin", m_log);
		auto& graph = rml::render::graph();
		graph.add_pass(std::make_unique<clouds::CloudDepthPass>(state));
		graph.add_pass(std::make_unique<clouds::CloudShadowPass>(state));
		graph.add_pass(std::make_unique<clouds::CloudTracePass>(state));
		graph.add_pass(std::make_unique<clouds::CloudReconstructPass>(state));
		graph.add_pass(std::make_unique<clouds::CloudCompositePass>(state));
	}

	void on_unload() override
	{
		auto& graph = rml::render::graph();
		for (const auto name : clouds::pass_names::ALL)
			graph.remove_pass(name);
	}

private:
	std::shared_ptr<spdlog::logger> m_log;
};

extern "C"
{
	RML_MOD_ABI_EXPORT ModBase* start_mod()
	{
		return new VolumetricCloudsMod();
	}

	RML_MOD_ABI_EXPORT void uninstall_mod(const ModBase* mod)
	{
		delete mod;
	}
}

RML_EXPORT_MOD_ABI_VERSION()
