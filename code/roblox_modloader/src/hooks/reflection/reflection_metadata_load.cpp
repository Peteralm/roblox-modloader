#include "RobloxModLoader/hooking/hooking.hpp"
#include "RobloxModLoader/internal/common.hpp"
#include "RobloxModLoader/internal/hooking/engine_hooks.hpp"
#include "roblox/reflection/class_registry.hpp"
#include "roblox/reflection/metadata_registry.hpp"

#include <mutex>

RML_LOG_SCOPE("Metadata");

void* rml::Hooks::reflection_metadata_load(void* self, const void* path)
{
	auto* result = Hooking::get_original<&Hooks::reflection_metadata_load>()(self, path);
	try
	{
		reflection::MetadataRegistry::instance().on_tree_loaded(self);
		static std::once_flag collisions_reported;
		std::call_once(collisions_reported, [] { reflection::ClassRegistry::instance().report_engine_collisions(); });
	}
	catch (const std::exception& e)
	{
		RML_ERROR("applying mod metadata after Reflection::load failed: {}", e.what());
	}
	catch (...)
	{
		RML_ERROR("applying mod metadata after Reflection::load failed with an unknown exception");
	}
	return result;
}
