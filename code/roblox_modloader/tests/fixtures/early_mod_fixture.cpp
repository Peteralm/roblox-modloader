#include <RobloxModLoader/logger/logger.hpp>
#include <RobloxModLoader/mod/global_init_mod.hpp>
#include <RobloxModLoader/mod/mod_base.hpp>
#include <stdexcept>
#if defined(_WIN32)
#include <windows.h>
#endif


std::shared_ptr<spdlog::logger> global_logger() { return spdlog::default_logger(); }
namespace
{
	int counts[9]{}; // early, start, load, unload, uninstall, caught, descriptors, normal ABI, early ABI
	int mode = 0;
	std::filesystem::path expected_root;
	class FixtureMod final : public ModBase
	{
		void on_load() override
		{
			++counts[2];
			if (mode == 6) throw std::runtime_error("on_load fixture");
			if (!expected_root.empty() && paths().root() != expected_root)
				throw std::runtime_error("catalog root was not adopted");
		}
		void on_unload() override { ++counts[3]; }
	};
	const int descriptor = 42;
	const RmlClassRegistrationV1 registration{1, sizeof(RmlClassRegistrationV1), "FixtureClass", "Instance",
	    &descriptor, &descriptor, nullptr, 0, 0};
}

extern "C" RML_MOD_ABI_EXPORT int fixture_count(int index) noexcept { return counts[index]; }
extern "C" RML_MOD_ABI_EXPORT void fixture_reset(int value) noexcept
{
	for (auto& count : counts) count = 0;
	mode = value;
	expected_root.clear();
}
#ifndef OMIT_START
extern "C" RML_MOD_ABI_EXPORT ModBase* start_mod() { ++counts[1]; return new FixtureMod; }
#endif
#ifndef OMIT_UNINSTALL
extern "C" RML_MOD_ABI_EXPORT void uninstall_mod(const ModBase* mod) { ++counts[4]; delete mod; }
#endif
#ifndef OMIT_NORMAL_ABI
extern "C" RML_MOD_ABI_EXPORT int rml_abi_version() noexcept(false)
{
	++counts[7];
#if defined(NORMAL_ABI_SEH) && defined(_WIN32)
	RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
#endif
#ifdef NORMAL_ABI_THROW
	throw std::runtime_error("normal ABI fixture");
#endif
#ifdef BAD_NORMAL_ABI
	return RML_ABI_VERSION + 1;
#else
	return RML_ABI_VERSION;
#endif
}
#endif
#ifndef OMIT_EARLY_ABI
extern "C" RML_MOD_ABI_EXPORT std::uint32_t rml_global_init_abi_version() noexcept
{
	try
	{
		++counts[8];
#if defined(EARLY_ABI_SEH) && defined(_WIN32)
		RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
#endif
#ifdef BAD_EARLY_ABI
		return RML_GLOBAL_INIT_ABI_VERSION + 1;
#else
		return RML_GLOBAL_INIT_ABI_VERSION;
#endif
	}
	catch (...) { return 0; }
}
#endif
#ifndef OMIT_EARLY
extern "C" RML_MOD_ABI_EXPORT int rml_global_init(const RmlGlobalInitContext* context) noexcept
{
	void* batch = nullptr;
	try
	{
		++counts[0];
		if (mode == 1) return 17;
		if (mode == 2) throw std::runtime_error("before publication");
#if defined(_WIN32)
		if (mode == 3) RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
#endif
		if (!context || context->abi_version != RML_GLOBAL_INIT_ABI_VERSION
		    || !context->mod_root_utf8 || !context->studio_build_utf8 || !context->log) return 18;
		context->log(1, context->mod_root_utf8);
		expected_root = std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(context->mod_root_utf8)));
		const auto* api = context->descriptors;
		batch = api->begin_batch(1);
		if (!batch) return 19;
		++counts[6];
		if (mode == 10) return 0; // Deliberately leave an outstanding reservation.
		if (mode == 4) throw std::runtime_error("reserved batch");
#if defined(_WIN32)
		if (mode == 5) RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
#endif
		auto payload = registration;
		if (mode == 7) payload.size = 8;
		if (mode == 8) payload.version = 2;
		if (api->reserve_class(batch, &payload) != 0)
		{
			if (mode == 9) { api->commit_batch(batch); return 0; }
			api->abort_batch(batch);
			return 20;
		}
		api->commit_batch(batch);
		if (mode == 11) return 22; // Publication cannot be rolled back or unloaded.
		return 0;
	}
	catch (...)
	{
		++counts[5];
		if (batch) context->descriptors->abort_batch(batch);
		return 21;
	}
}
#endif
