#include <doctest/doctest.h>
#include "native/early_mod_registry.hpp"
#include "native/native_mod_loader.hpp"
#include <RobloxModLoader/logger/logger.hpp>
#include <windows.h>
#include <array>
#include <string>

// Isolate core lifecycle tests from Studio's logging/configuration startup.
std::shared_ptr<spdlog::logger> global_logger() { return spdlog::default_logger(); }
std::shared_ptr<spdlog::logger> rml::Logger::get_logger(const std::string&) { return global_logger(); }

namespace
{
	using namespace rml;
	using namespace rml::native;
	struct Descriptors
	{
		int begins = 0, reserves = 0, commits = 0, aborts = 0, logs = 0;
		bool reject = false;
		const void* published = nullptr;
		const void* reserved = nullptr;
		std::string root;
		static inline Descriptors* current;
		Descriptors() { current = this; }
		RmlDescriptorRegistrationApi api{RML_DESCRIPTOR_REGISTRATION_API_VERSION, sizeof(RmlDescriptorRegistrationApi),
		    [](const char*) noexcept -> void* { return nullptr; },
		    []() noexcept { return true; },
		    [](std::uint32_t) noexcept -> void* { ++current->begins; return current; },
		    [](void*, const RmlClassRegistrationV1* registration) noexcept {
			    ++current->reserves;
			    if (current->reject) return 1;
			    current->reserved = registration->descriptor;
			    return 0;
		    },
		    [](void*) noexcept { ++current->aborts; current->reserved = nullptr; },
		    [](void*) noexcept { ++current->commits; current->published = current->reserved; current->reserved = nullptr; },
		    [](const void* descriptor) noexcept -> const void* { return descriptor; }};
		RmlGlobalInitContext context{1, nullptr, "fixture-studio", [](int, const char* message) noexcept {
			++current->logs;
			try { current->root = message; } catch (...) {}
		}, &api};
	};
	struct Fixture
	{
		std::filesystem::path path;
		HMODULE handle;
		int (*count)(int) noexcept;
		Fixture(const char* name = "success", int mode = 0) :
		    path(std::filesystem::path(RML_EARLY_FIXTURE_DIR) / (std::string("early_fixture_") + name + ".dll")),
		    handle(LoadLibraryW(path.c_str()))
		{
			REQUIRE(handle != nullptr);
			count = reinterpret_cast<decltype(count)>(GetProcAddress(handle, "fixture_count"));
			auto reset = reinterpret_cast<void (*)(int) noexcept>(GetProcAddress(handle, "fixture_reset"));
			REQUIRE(count != nullptr);
			REQUIRE(reset != nullptr);
			reset(mode);
		}
		~Fixture() { FreeLibrary(handle); }
		ModDefinition definition() const
		{
			return {"fixture", path.parent_path(), path, {}, "Fixture", 0, true, true, config::ModLoadPhase::GlobalInit};
		}
	};
}

TEST_CASE("early mod registry preflights every mandatory export and ABI before entry")
{
	for (const auto* name : {"missing_start", "missing_uninstall", "missing_normal_abi", "missing_early_abi",
	         "missing_early", "bad_normal_abi", "bad_early_abi", "normal_abi_seh", "early_abi_seh", "normal_abi_throw"})
	{
		CAPTURE(name);
		Fixture fixture(name);
		Descriptors descriptors;
		EarlyModRegistry registry;
		const std::array definitions{fixture.definition()};
		registry.attach_all(definitions, descriptors.context);
		CHECK(registry.status(fixture.path) == EarlyModStatus::Failed);
		CHECK_FALSE(registry.is_pinned(fixture.path));
		CHECK_FALSE(registry.adopt(fixture.path).has_value());
		CHECK(fixture.count(0) == 0);
		CHECK(fixture.count(6) == 0);
		CHECK(descriptors.begins == 0);
		CHECK(descriptors.commits == 0);
		events::EventManager events;
		NativeModLoader loader(events, registry);
		CHECK_FALSE(loader.load(fixture.path).has_value());
		CHECK(fixture.count(1) == 0);
	}
}

TEST_CASE("early mod registry success attaches once and adopts through normal lifecycle once")
{
	Fixture fixture;
	Descriptors descriptors;
	EarlyModRegistry registry;
	const std::array definitions{fixture.definition()};
	registry.attach_all(definitions, descriptors.context);
	registry.attach_all(definitions, descriptors.context);
	CHECK(registry.status(fixture.path) == EarlyModStatus::Attached);
	CHECK(registry.is_pinned(fixture.path));
	CHECK(descriptors.root == fixture.path.parent_path().string());
	CHECK(descriptors.logs == 1);
	{
		events::EventManager events;
		NativeModLoader loader(events, registry);
		REQUIRE(loader.load(fixture.path).has_value());
		REQUIRE(loader.load(fixture.path).has_value());
		CHECK(registry.status(fixture.path) == EarlyModStatus::Adopted);
		CHECK_FALSE(registry.adopt(fixture.path).has_value());
		CHECK_FALSE(loader.unload(fixture.path).has_value());
		CHECK_FALSE(loader.reload(fixture.path).has_value());
		loader.unload_all();
		CHECK(loader.load(fixture.path).has_value());
	}
	CHECK(fixture.count(0) == 1);
	CHECK(fixture.count(1) == 1);
	CHECK(fixture.count(2) == 1);
	CHECK(fixture.count(7) == 1);
	CHECK(fixture.count(8) == 1);
	CHECK(descriptors.reserves == 1);
	CHECK(descriptors.commits == 1);
	CHECK(fixture.count(3) == 0);
	CHECK(fixture.count(4) == 0);
	REQUIRE(descriptors.published != nullptr);
	CHECK(*static_cast<const int*>(descriptors.published) == 42);
}

TEST_CASE("early mod registry respects enabled phase and auto_load")
{
	Fixture fixture;
	Descriptors descriptors;
	EarlyModRegistry registry;
	auto definition = fixture.definition();
	SUBCASE("disabled") { definition.enabled = false; }
	SUBCASE("manual") { definition.auto_load = false; }
	SUBCASE("normal") { definition.load_phase = config::ModLoadPhase::Normal; }
	SUBCASE("no native entry") { definition.native_entry.reset(); }
	const std::array definitions{definition};
	registry.attach_all(definitions, descriptors.context);
	CHECK(registry.status(fixture.path) == EarlyModStatus::NotFound);
	CHECK(fixture.count(0) == 0);
	CHECK(descriptors.begins == 0);
}

TEST_CASE("early mod registry contains early failures exceptions and SEH without retry")
{
	for (int mode : {1, 2, 3, 4, 5, 7, 8})
	{
		CAPTURE(mode);
		Fixture fixture("success", mode);
		Descriptors descriptors;
		EarlyModRegistry registry;
		const std::array definitions{fixture.definition()};
		registry.attach_all(definitions, descriptors.context);
		registry.attach_all(definitions, descriptors.context);
		CHECK(registry.status(fixture.path) == EarlyModStatus::Failed);
		CHECK(registry.is_pinned(fixture.path));
		CHECK_FALSE(registry.adopt(fixture.path).has_value());
		CHECK(fixture.count(0) == 1);
		CHECK(fixture.count(5) == ((mode == 2 || mode == 4) ? 1 : 0));
		CHECK(descriptors.commits == 0);
		CHECK(descriptors.reserves == 0);
		CHECK(descriptors.aborts == ((mode >= 4) ? 1 : 0));
	}
}

TEST_CASE("early mod registry rejects incompatible descriptor APIs before entry")
{
	Fixture fixture;
	Descriptors descriptors;
	SUBCASE("context ABI") { descriptors.context.abi_version = 2; }
	SUBCASE("API version") { descriptors.api.version = RML_DESCRIPTOR_REGISTRATION_API_VERSION + 1; }
	SUBCASE("short API") { descriptors.api.size = 8; }
	SUBCASE("missing API") { descriptors.context.descriptors = nullptr; }
	SUBCASE("missing callback") { descriptors.api.commit_batch = nullptr; }
	EarlyModRegistry registry;
	const std::array definitions{fixture.definition()};
	registry.attach_all(definitions, descriptors.context);
	CHECK(registry.status(fixture.path) == EarlyModStatus::Failed);
	CHECK(fixture.count(0) == 0);
	CHECK(descriptors.begins == 0);
}

TEST_CASE("early mod registry aborts a fallible descriptor reservation")
{
	Fixture fixture;
	Descriptors descriptors;
	descriptors.reject = true;
	EarlyModRegistry registry;
	const std::array definitions{fixture.definition()};
	registry.attach_all(definitions, descriptors.context);
	CHECK(registry.status(fixture.path) == EarlyModStatus::Failed);
	CHECK(descriptors.reserves == 1);
	CHECK(descriptors.aborts == 1);
	CHECK(descriptors.commits == 0);
}

TEST_CASE("early mod registry pins modules even when normal on_load fails")
{
	Fixture fixture("success", 6);
	Descriptors descriptors;
	EarlyModRegistry registry;
	const std::array definitions{fixture.definition()};
	registry.attach_all(definitions, descriptors.context);
	events::EventManager events;
	NativeModLoader loader(events, registry);
	CHECK_FALSE(loader.load(fixture.path).has_value());
	CHECK_FALSE(loader.load(fixture.path).has_value());
	CHECK_FALSE(loader.unload(fixture.path).has_value());
	CHECK_FALSE(loader.reload(fixture.path).has_value());
	CHECK(registry.is_pinned(fixture.path));
	CHECK(fixture.count(0) == 1);
	CHECK(fixture.count(1) == 1);
	CHECK(fixture.count(4) == 1);
	CHECK(*static_cast<const int*>(descriptors.published) == 42);
}

TEST_CASE("early mod registry rejects incomplete or invalid commits")
{
	for (int mode : {9, 10})
	{
		Fixture fixture("success", mode);
		Descriptors descriptors;
		descriptors.reject = true;
		EarlyModRegistry registry;
		const std::array definitions{fixture.definition()};
		registry.attach_all(definitions, descriptors.context);
		CHECK(registry.status(fixture.path) == EarlyModStatus::Failed);
		CHECK(descriptors.aborts == 1);
		CHECK(descriptors.commits == 0);
		CHECK(descriptors.published == nullptr);
	}
}

TEST_CASE("early mod registry retains published storage after a late failure")
{
	Fixture fixture("success", 11);
	Descriptors descriptors;
	EarlyModRegistry registry;
	const std::array definitions{fixture.definition()};
	registry.attach_all(definitions, descriptors.context);
	CHECK(registry.status(fixture.path) == EarlyModStatus::Failed);
	CHECK(registry.is_pinned(fixture.path));
	CHECK(descriptors.commits == 1);
	CHECK(descriptors.aborts == 0);
	CHECK(*static_cast<const int*>(descriptors.published) == 42);
}

TEST_CASE("early mod registry leaves normal native lifecycle unloadable")
{
	Fixture fixture;
	EarlyModRegistry registry;
	events::EventManager events;
	NativeModLoader loader(events, registry);
	REQUIRE(loader.load(fixture.path).has_value());
	REQUIRE(loader.reload(fixture.path).has_value());
	loader.unload_all();
	CHECK(fixture.count(0) == 0);
	CHECK(fixture.count(1) == 2);
	CHECK(fixture.count(2) == 2);
	CHECK(fixture.count(3) == 2);
	CHECK(fixture.count(4) == 2);
}

TEST_CASE("early mod registry canonical aliases cannot repeat adoption")
{
	Fixture fixture;
	Descriptors descriptors;
	EarlyModRegistry registry;
	const std::array definitions{fixture.definition()};
	registry.attach_all(definitions, descriptors.context);
	auto alias = fixture.path.parent_path() / "." / fixture.path.filename();
	auto native = alias.native();
	CharUpperBuffW(native.data(), static_cast<DWORD>(native.size()));
	alias = native;
	CHECK(registry.is_pinned(alias));
	REQUIRE(registry.adopt(alias).has_value());
	CHECK_FALSE(registry.adopt(fixture.path).has_value());
	CHECK(registry.status(fixture.path) == EarlyModStatus::Adopted);
}

TEST_CASE("early mod registry adoption preserves the unified catalog root")
{
	Fixture fixture;
	Descriptors descriptors;
	EarlyModRegistry registry;
	auto definition = fixture.definition();
	definition.root /= "catalog-root";
	const std::array definitions{definition};
	registry.attach_all(definitions, descriptors.context);
	REQUIRE(registry.status(fixture.path) == EarlyModStatus::Attached);
	events::EventManager events;
	NativeModLoader loader(events, registry);
	CHECK(loader.load(fixture.path).has_value());
}
