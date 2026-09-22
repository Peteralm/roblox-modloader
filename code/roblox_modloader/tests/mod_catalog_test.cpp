#include "mod/mod_catalog.hpp"
#include "mod/mod_manager.hpp"

#include <atomic>
#include <chrono>
#include <doctest/doctest.h>
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

		[[nodiscard]] const ModDefinition* find_mod(const ModCatalogResult& catalog, const std::string_view folder_id)
		{
			const auto found = std::ranges::find(catalog.mods, folder_id, &ModDefinition::folder_id);
			return found == catalog.mods.end() ? nullptr : &*found;
		}

		struct LoadCall
		{
			std::string loader;
			std::string file;

			bool operator==(const LoadCall&) const = default;
		};

		class RecordingLoader final : public IModLoader
		{
		public:
			RecordingLoader(std::string name, std::vector<LoadCall>& calls) :
			    m_name(std::move(name)),
			    m_calls(calls)
			{
			}

			std::expected<void, std::string> load(const std::filesystem::path& path) override
			{
				m_calls.push_back({m_name, path.filename().string()});
				return {};
			}
			std::expected<void, std::string> unload(const std::filesystem::path&) override
			{
				return {};
			}
			std::expected<void, std::string> reload(const std::filesystem::path&) override
			{
				return {};
			}
			void unload_all() override
			{
			}
			std::vector<std::filesystem::path> extensions() const override
			{
				return {".dll"};
			}

		private:
			std::string m_name;
			std::vector<LoadCall>& m_calls;
		};
	} // namespace

	TEST_CASE("mod catalog discovers official manifestless native layout")
	{
		CatalogSandbox sandbox;
		sandbox.file("mods/canonical-folder/native/entry.dll");

		const auto catalog = discover_mods(sandbox.root());

		REQUIRE(catalog.errors.empty());
		REQUIRE(catalog.mods.size() == 1);
		const auto& mod = catalog.mods.front();
		CHECK(mod.folder_id == "canonical-folder");
		CHECK(mod.name == "canonical-folder");
		REQUIRE(mod.native_entry.has_value());
		CHECK(mod.native_entry->filename() == "entry.dll");
		CHECK(mod.dotnet_entries.empty());
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

		const auto catalog = discover_mods(sandbox.root());
		const auto* mod = find_mod(catalog, "folder-key");

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

		const auto catalog = discover_mods(sandbox.root());

		REQUIRE(find_mod(catalog, "static") != nullptr);
		CHECK(find_mod(catalog, "static")->priority == 22);
		REQUIRE(find_mod(catalog, "no-manifest") != nullptr);
		CHECK(find_mod(catalog, "no-manifest")->priority == 0);
	}

	TEST_CASE("mod catalog keeps auto load policy at pure managed and mixed roots")
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

		const auto catalog = discover_mods(sandbox.root());
		const auto* managed = find_mod(catalog, "managed");
		const auto* mixed = find_mod(catalog, "mixed");

		REQUIRE(managed != nullptr);
		CHECK_FALSE(managed->auto_load);
		CHECK_FALSE(managed->native_entry.has_value());
		REQUIRE(managed->dotnet_entries.size() == 1);
		REQUIRE(mixed != nullptr);
		CHECK_FALSE(mixed->auto_load);
		CHECK(mixed->native_entry.has_value());
		REQUIRE(mixed->dotnet_entries.size() == 1);
	}

	TEST_CASE("mod catalog selects global init entry without native dependencies")
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

		const auto catalog = discover_mods(sandbox.root());
		const auto* mod = find_mod(catalog, "early");

		REQUIRE(catalog.errors.empty());
		REQUIRE(mod != nullptr);
		REQUIRE(mod->native_entry.has_value());
		CHECK(mod->native_entry->filename() == "Early.dll");
		CHECK(mod->load_phase == config::ModLoadPhase::GlobalInit);
		CHECK_FALSE(mod->enabled);
		CHECK_FALSE(mod->auto_load);
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

		const auto catalog = discover_mods(sandbox.root());

		REQUIRE(catalog.mods.size() == 1);
		CHECK(catalog.mods.front().folder_id == "good");
		CHECK(find_mod(catalog, "missing") == nullptr);
		CHECK(find_mod(catalog, "malformed") == nullptr);
		CHECK(find_mod(catalog, "invalid-policy") == nullptr);
		CHECK(catalog.errors.size() == 3);
	}

	TEST_CASE("mod catalog rejects identified invalid loader policy fail closed")
	{
		CatalogSandbox sandbox;
		sandbox.file("mods/blocked/native/Blocked.dll");
		sandbox.file("mods/other/native/Other.dll");
		sandbox.file("config.toml", R"(
[[mods]]
id = "blocked"
[mods.runtime]
auto_load = "invalid"
)");

		const auto catalog = discover_mods(sandbox.root());

		CHECK(find_mod(catalog, "blocked") == nullptr);
		REQUIRE(find_mod(catalog, "other") != nullptr);
		CHECK(catalog.errors.size() == 1);
	}

	TEST_CASE("mod catalog rejects unsafe and incomplete global entries")
	{
		CatalogSandbox sandbox;
		sandbox.file("mods/early/native/Early.dll");
		sandbox.file("mods/early/mod.toml", "[runtime]\nload_phase='global_init'\nentry='Early.dll'\n");
		sandbox.file("mods/traversal/native/Entry.dll");
		sandbox.file("mods/traversal/Outside.dll");
		sandbox.file("mods/traversal/mod.toml", "name='Traversal'\n[runtime]\nentry='../Outside.dll'\n");

		const auto catalog = discover_mods(sandbox.root());

		CHECK(catalog.mods.empty());
		CHECK(catalog.errors.size() == 2);
	}

	TEST_CASE("mod catalog orders effective priority then canonical root path")
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

		const auto catalog = discover_mods(sandbox.root());

		REQUIRE(catalog.mods.size() == 3);
		CHECK(catalog.mods[0].folder_id == "high");
		CHECK(catalog.mods[1].folder_id == "alpha");
		CHECK(catalog.mods[2].folder_id == "zeta");
	}

	TEST_CASE("mod catalog keeps one folder per declared identity")
	{
		CatalogSandbox sandbox;
		sandbox.file("mods/editor/dotnet/Editor.dll");
		sandbox.file("mods/editor/mod.toml", R"(
name = "Script Editor Webview"
[runtime]
priority = 500
)");
		sandbox.file("mods/editor.bak/dotnet/Editor.dll");
		sandbox.file("mods/editor.bak/mod.toml", R"(
name = "Script Editor Webview"
[runtime]
priority = 500
)");

		const auto catalog = discover_mods(sandbox.root());

		REQUIRE(catalog.mods.size() == 1);
		CHECK(catalog.mods.front().folder_id == "editor");
		REQUIRE(catalog.errors.size() == 1);
		CHECK(catalog.errors.front().message.contains("already claims the identity"));
	}

	TEST_CASE("mod catalog manager loads exact ordered entries and defers gated roots")
	{
		CatalogSandbox sandbox;
		const auto add_mixed = [&sandbox](const std::string_view id, const std::string_view native, const std::string_view managed) {
			sandbox.file(std::filesystem::path("mods") / id / "native" / native);
			sandbox.file(std::filesystem::path("mods") / id / "dotnet" / managed);
		};
		add_mixed("high", "High.dll", "High.Managed.dll");
		add_mixed("aardvark", "Aardvark.dll", "Aardvark.Managed.dll");
		add_mixed("beta", "Beta.dll", "Beta.Managed.dll");
		add_mixed("mixed-off", "MixedOff.dll", "MixedOff.Managed.dll");
		sandbox.file("mods/managed-off/dotnet/ManagedOff.dll");
		sandbox.file("mods/early/native/Early.dll");
		sandbox.file("mods/early/mod.toml", "name='Early'\n[runtime]\nload_phase='global_init'\nentry='Early.dll'\n");
		sandbox.file("config.toml", R"(
[[mods]]
id = "early"
[mods.runtime]
priority = 20

[[mods]]
id = "high"
[mods.runtime]
priority = 10

[[mods]]
id = "aardvark"
[mods.runtime]
priority = 5

[[mods]]
id = "beta"
[mods.runtime]
priority = 5

[[mods]]
id = "mixed-off"
[mods.runtime]
auto_load = false

[[mods]]
id = "managed-off"
[mods.runtime]
auto_load = false
)");

		const auto catalog = discover_mods(sandbox.root());
		std::vector<LoadCall> calls;
		RecordingLoader native("native", calls);
		RecordingLoader dotnet("dotnet", calls);
		const auto errors = ModManager::load_catalog(catalog, &native, &dotnet);

		CHECK(errors.empty());
		CHECK(calls == std::vector<LoadCall>{{"native", "High.dll"}, {"dotnet", "High.Managed.dll"}, {"native", "Aardvark.dll"}, {"dotnet", "Aardvark.Managed.dll"}, {"native", "Beta.dll"}, {"dotnet", "Beta.Managed.dll"}});
	}


	TEST_CASE("mod catalog selects one managed entry beside private dependencies")
	{
		CatalogSandbox sandbox;
		sandbox.file("mods/mixed/native/Early.dll");
		sandbox.file("mods/mixed/dotnet/CommandSupervisor.UI.dll");
		sandbox.file("mods/mixed/dotnet/StudioDock.dll");
		sandbox.file("mods/mixed/mod.toml", R"(
name = "Mixed"
[runtime]
load_phase = "global_init"
entry = "Early.dll"
managed_entry = "CommandSupervisor.UI.dll"
)");

		const auto catalog = discover_mods(sandbox.root());
		REQUIRE(catalog.errors.empty());
		REQUIRE(catalog.mods.size() == 1);
		REQUIRE(catalog.mods[0].dotnet_entries.size() == 1);
		CHECK(catalog.mods[0].dotnet_entries[0].filename() == "CommandSupervisor.UI.dll");
	}

} // namespace rml
