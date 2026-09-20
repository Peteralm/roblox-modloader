#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "RobloxModLoader/config/config_serialization.hpp"

namespace rml::config
{

TEST_CASE("mod load phase defaults to normal")
{
	const auto parsed = toml::parse("name = 'x'");
	REQUIRE(static_cast<bool>(parsed));
	const auto config = serialization::mod_config_from_toml(parsed.table());
	REQUIRE(static_cast<bool>(config));
	CHECK(config->runtime.load_phase == ModLoadPhase::Normal);
}

TEST_CASE("global_init parses and round-trips")
{
	const auto parsed = toml::parse("name='x'\n[runtime]\nload_phase='global_init'");
	REQUIRE(static_cast<bool>(parsed));
	const auto config = serialization::mod_config_from_toml(parsed.table());
	REQUIRE(static_cast<bool>(config));
	CHECK(config->runtime.load_phase == ModLoadPhase::GlobalInit);
	const auto serialized = serialization::mod_config_to_toml(*config);
	REQUIRE(static_cast<bool>(serialized.at_path("runtime.load_phase")));
	CHECK(serialized.at_path("runtime.load_phase").value<std::string>() == "global_init");
}

TEST_CASE("unknown load phase is rejected")
{
	const auto parsed = toml::parse("name='x'\n[runtime]\nload_phase='later'");
	REQUIRE(static_cast<bool>(parsed));
	CHECK_FALSE(static_cast<bool>(serialization::mod_config_from_toml(parsed.table())));
}

} // namespace rml::config
