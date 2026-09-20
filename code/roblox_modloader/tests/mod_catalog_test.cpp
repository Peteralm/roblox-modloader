#include <doctest/doctest.h>

#include "mod/mod_catalog.hpp"

#include <atomic>
#include <chrono>
#include <fstream>

namespace rml
{
namespace
{
	class CatalogSandbox
	{
	public:
		CatalogSandbox()
		{
			static std::atomic_uint64_t sequence{};
			const auto suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-"
			                  + std::to_string(sequence.fetch_add(1));
			m_root = std::filesystem::temp_directory_path() / ("rml-mod-catalog-" + suffix);
			std::filesystem::create_directories(m_root / "mods");
		}

		~CatalogSandbox()
		{
			std::error_code error;
			std::filesystem::remove_all(m_root, error);
		}

		[[nodiscard]] const std::filesystem::path& root() const noexcept
		{
			return m_root;
		}

		void file(const std::filesystem::path& relative, const std::string_view contents = {}) const
		{
			const auto path = m_root / relative;
			std::filesystem::create_directories(path.parent_path());
			std::ofstream stream(path, std::ios::binary);
			stream << contents;
		}

	private:
		std::filesystem::path m_root;
	};

	[[nodiscard]] const ModRootDefinition* find_root(const ModCatalogResult& catalog, const std::string_view folder_id)
	{
		const auto found = std::ranges::find(catalog.roots, folder_id, &ModRootDefinition::folder_id);
		return found == catalog.roots.end() ? nullptr : &*found;
	}

	[[nodiscard]] const NativeModDefinition* find_native(
	    const ModCatalogResult& catalog, const std::string_view folder_id)
	{
		const auto found = std::ranges::find(catalog.native_mods, folder_id, &NativeModDefinition::folder_id);
		return found == catalog.native_mods.end() ? nullptr : &*found;
	}
} // namespace

TEST_CASE("mod catalog discovers official manifestless native layout")
{
	CatalogSandbox sandbox;
	sandbox.file("mods/canonical-folder/native/entry.dll");

	const auto catalog = discover_native_mods(sandbox.root());

	REQUIRE(catalog.errors.empty());
	REQUIRE(catalog.roots.size() == 1);
	REQUIRE(catalog.native_mods.size() == 1);
	const auto& mod = catalog.native_mods.front();
	CHECK(mod.folder_id == "canonical-folder");
	CHECK(mod.name == "canonical-folder");
	CHECK(mod.dll.filename() == "entry.dll");
	CHECK(mod.priority == 0);
	CHECK(mod.enabled);
	CHECK(mod.auto_load);
	CHECK(mod.load_phase == config::ModLoadPhase::Normal);
}

TEST_CASE("mod catalog merges explicit id policy over manifest defaults")
{
	CatalogSandbox sandbox;
	sandbox.file("mods/folder-key/native/main.dll");
	sandbox.file("mods/folder-key/mod.toml", R"(
name = "Static Name"
[runtime]
entry = "main.dll"
priority = 3
)");
	sandbox.file("config.toml", R"(
[[mods]]
id = "folder-key"
name = "Unrelated Display Name"
[mods.runtime]
enabled = false
auto_load = false
priority = 17
)");

	const auto catalog = discover_native_mods(sandbox.root());
	const auto* mod = find_native(catalog, "folder-key");

	REQUIRE(mod != nullptr);
	CHECK(mod->name == "Static Name");
	CHECK_FALSE(mod->enabled);
	CHECK_FALSE(mod->auto_load);
	CHECK(mod->priority == 17);
}

TEST_CASE("mod catalog supports legacy name policy only with static identity")
{
	CatalogSandbox sandbox;
	sandbox.file("mods/static/native/static.dll");
	sandbox.file("mods/static/mod.toml", "name = 'Legacy Name'\n[runtime]\nentry = 'static.dll'\n");
	sandbox.file("mods/no-manifest/native/plain.dll");
	sandbox.file("config.toml", R"(
[[mods]]
name = "Legacy Name"
[mods.runtime]
priority = 22

[[mods]]
name = "no-manifest"
[mods.runtime]
priority = 99
)");

	const auto catalog = discover_native_mods(sandbox.root());

	REQUIRE(find_native(catalog, "static") != nullptr);
	CHECK(find_native(catalog, "static")->priority == 22);
	REQUIRE(find_native(catalog, "no-manifest") != nullptr);
	CHECK(find_native(catalog, "no-manifest")->priority == 0);
}

TEST_CASE("mod catalog keeps auto load policy at the mod root")
{
	CatalogSandbox sandbox;
	sandbox.file("mods/managed/dotnet/Managed.dll");
	sandbox.file("mods/mixed/native/Mixed.dll");
	sandbox.file("mods/mixed/dotnet/Mixed.Managed.dll");
	sandbox.file("config.toml", R"(
[[mods]]
id = "managed"
[mods.runtime]
auto_load = false

[[mods]]
id = "mixed"
[mods.runtime]
auto_load = false
)");

	const auto catalog = discover_native_mods(sandbox.root());
	const auto* managed = find_root(catalog, "managed");
	const auto* mixed = find_root(catalog, "mixed");

	REQUIRE(managed != nullptr);
	CHECK_FALSE(managed->auto_load);
	CHECK(find_native(catalog, "managed") == nullptr);
	REQUIRE(mixed != nullptr);
	CHECK_FALSE(mixed->auto_load);
	REQUIRE(find_native(catalog, "mixed") != nullptr);
	CHECK_FALSE(find_native(catalog, "mixed")->auto_load);
}

TEST_CASE("mod catalog retains disabled global init metadata without loading dependencies")
{
	CatalogSandbox sandbox;
	sandbox.file("mods/early/native/Early.dll");
	sandbox.file("mods/early/native/Dependency.dll");
	sandbox.file("mods/early/mod.toml", R"(
name = "Early Mod"
[runtime]
load_phase = "global_init"
entry = "Early.dll"
)");
	sandbox.file("config.toml", R"(
[[mods]]
id = "early"
[mods.runtime]
enabled = false
auto_load = false
)");

	const auto catalog = discover_native_mods(sandbox.root());
	const auto* mod = find_native(catalog, "early");

	REQUIRE(catalog.errors.empty());
	REQUIRE(mod != nullptr);
	CHECK(mod->dll.filename() == "Early.dll");
	CHECK(mod->load_phase == config::ModLoadPhase::GlobalInit);
	CHECK_FALSE(mod->enabled);
	CHECK_FALSE(mod->auto_load);
	CHECK(catalog.native_mods.size() == 1);
}

TEST_CASE("mod catalog isolates missing entries and malformed manifests")
{
	CatalogSandbox sandbox;
	sandbox.file("mods/good/native/Good.dll");
	sandbox.file("mods/missing/native/Other.dll");
	sandbox.file("mods/missing/mod.toml", "name='Missing'\n[runtime]\nentry='Absent.dll'\n");
	sandbox.file("mods/malformed/native/Broken.dll");
	sandbox.file("mods/malformed/mod.toml", "name = [\n");
	sandbox.file("mods/invalid-policy/native/Invalid.dll");
	sandbox.file("mods/invalid-policy/mod.toml", "name='Invalid'\n[runtime]\nauto_load='never'\n");

	const auto catalog = discover_native_mods(sandbox.root());

	REQUIRE(catalog.native_mods.size() == 1);
	CHECK(catalog.native_mods.front().folder_id == "good");
	CHECK(find_root(catalog, "missing") == nullptr);
	CHECK(find_root(catalog, "malformed") == nullptr);
	CHECK(find_root(catalog, "invalid-policy") == nullptr);
	CHECK(catalog.errors.size() == 3);
}

TEST_CASE("mod catalog rejects global init without static identity")
{
	CatalogSandbox sandbox;
	sandbox.file("mods/early/native/Early.dll");
	sandbox.file("mods/early/mod.toml", "[runtime]\nload_phase='global_init'\nentry='Early.dll'\n");

	const auto catalog = discover_native_mods(sandbox.root());

	CHECK(catalog.roots.empty());
	CHECK(catalog.native_mods.empty());
	REQUIRE(catalog.errors.size() == 1);
}

TEST_CASE("mod catalog orders effective priority then canonical dll path")
{
	CatalogSandbox sandbox;
	sandbox.file("mods/zeta/native/z.dll");
	sandbox.file("mods/alpha/native/a.dll");
	sandbox.file("mods/high/native/high.dll");
	sandbox.file("config.toml", R"(
[[mods]]
id = "zeta"
[mods.runtime]
priority = 5

[[mods]]
id = "alpha"
[mods.runtime]
priority = 5

[[mods]]
id = "high"
[mods.runtime]
priority = 10
)");

	const auto catalog = discover_native_mods(sandbox.root());

	REQUIRE(catalog.native_mods.size() == 3);
	CHECK(catalog.native_mods[0].folder_id == "high");
	CHECK(catalog.native_mods[1].folder_id == "alpha");
	CHECK(catalog.native_mods[2].folder_id == "zeta");
}

} // namespace rml
