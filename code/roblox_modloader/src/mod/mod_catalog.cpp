#include "mod_catalog.hpp"

#include "RobloxModLoader/config/config_serialization.hpp"
#include "mod_kind.hpp"

#include <algorithm>
#include <fstream>
#include <limits>
#include <optional>
#include <ranges>
#include <string_view>
#include <toml++/toml.hpp>

namespace rml
{
	namespace
	{
		struct PolicyOverride
		{
			std::optional<std::string> id;
			std::optional<std::string> name;
			std::optional<bool> enabled;
			std::optional<bool> auto_load;
			std::optional<std::int32_t> priority;
			bool valid{true};
		};

		struct ManifestData
		{
			std::string name;
			config::ModConfig::Runtime runtime;
			std::optional<std::filesystem::path> entry;
			std::optional<std::filesystem::path> managed_entry;
			bool has_static_identity{};
			bool has_explicit_load_phase{};
		};


		[[nodiscard]] bool is_missing(const std::error_code& error)
		{
			return error == std::errc::no_such_file_or_directory;
		}
		struct NativeSelection
		{
			std::optional<std::filesystem::path> entry;
			bool valid{true};
		};

		[[nodiscard]] std::optional<toml::table> read_toml(const std::filesystem::path& path, ModCatalogResult& result)
		{
			std::ifstream stream(path, std::ios::binary);
			if (!stream)
			{
				result.errors.push_back({path, "cannot open TOML file"});
				return std::nullopt;
			}

			const auto parsed = toml::parse(stream, path.string());
			if (!parsed)
			{
				const auto& error = parsed.error();
				result.errors.push_back({path,
				    std::string(error.description()) + " (line " + std::to_string(error.source().begin.line) + ", column "
				        + std::to_string(error.source().begin.column) + ")"});
				return std::nullopt;
			}
			return std::move(parsed).table();
		}

		[[nodiscard]] bool read_optional_string(const toml::table& table, const std::string_view key, std::optional<std::string>& value)
		{
			const auto node = table[key];
			if (!node)
				return true;
			const auto parsed = node.value<std::string>();
			if (!parsed)
				return false;
			value = *parsed;
			return true;
		}

		[[nodiscard]] bool read_optional_bool(const toml::table& table, const std::string_view key, std::optional<bool>& value)
		{
			const auto node = table[key];
			if (!node)
				return true;
			const auto parsed = node.value<bool>();
			if (!parsed)
				return false;
			value = *parsed;
			return true;
		}

		[[nodiscard]] bool read_optional_priority(const toml::table& table, std::optional<std::int32_t>& value)
		{
			const auto node = table["priority"];
			if (!node)
				return true;
			const auto parsed = node.value<std::int64_t>();
			if (!parsed || *parsed < std::numeric_limits<std::int32_t>::min() || *parsed > std::numeric_limits<std::int32_t>::max())
				return false;
			value = static_cast<std::int32_t>(*parsed);
			return true;
		}

		[[nodiscard]] std::vector<PolicyOverride> load_policies(const std::filesystem::path& loader_root, ModCatalogResult& result)
		{
			const auto path = loader_root / "config.toml";
			std::error_code error;
			if (!std::filesystem::exists(path, error) || error)
				return {};

			const auto table = read_toml(path, result);
			if (!table)
				return {};
			const auto mods_node = (*table)["mods"];
			if (!mods_node)
				return {};
			const auto* mods = mods_node.as_array();
			if (!mods)
			{
				result.errors.push_back({path, "'mods' must be an array of tables"});
				return {};
			}

			std::vector<PolicyOverride> policies;
			for (std::size_t index = 0; index < mods->size(); ++index)
			{
				const auto* mod = (*mods)[index].as_table();
				if (!mod)
				{
					result.errors.push_back({path, "mods[" + std::to_string(index) + "] must be a table"});
					continue;
				}

				PolicyOverride policy;
				if (!read_optional_string(*mod, "id", policy.id) || !read_optional_string(*mod, "name", policy.name))
				{
					result.errors.push_back({path, "mods[" + std::to_string(index) + "] has a non-string identity"});
					continue;
				}
				if (!policy.id && !policy.name)
				{
					result.errors.push_back({path, "mods[" + std::to_string(index) + "] has no id or name"});
					continue;
				}

				if (const auto runtime_node = (*mod)["runtime"])
				{
					const auto* runtime = runtime_node.as_table();
					if (!runtime || !read_optional_bool(*runtime, "enabled", policy.enabled)
					    || !read_optional_bool(*runtime, "auto_load", policy.auto_load)
					    || !read_optional_priority(*runtime, policy.priority))
					{
						policy.valid = false;
						result.errors.push_back({path, "mods[" + std::to_string(index) + "] has invalid runtime policy"});
					}
				}
				policies.push_back(std::move(policy));
			}
			return policies;
		}

		[[nodiscard]] std::optional<ManifestData> load_manifest(const std::filesystem::path& path, ModCatalogResult& result)
		{
			const auto table = read_toml(path, result);
			if (!table)
				return std::nullopt;

			ManifestData manifest;
			if (const auto name_node = (*table)["name"])
			{
				const auto name = name_node.value<std::string>();
				if (!name || name->empty())
				{
					result.errors.push_back({path, "'name' must be a non-empty string"});
					return std::nullopt;
				}
				manifest.name = *name;
				manifest.has_static_identity = true;
			}

			const auto parsed = config::serialization::mod_config_from_toml(*table);
			if (!parsed)
			{
				result.errors.push_back({path, "invalid mod metadata"});
				return std::nullopt;
			}
			manifest.runtime = parsed->runtime;

			if (const auto runtime_node = (*table)["runtime"])
			{
				const auto* runtime = runtime_node.as_table();
				std::optional<bool> enabled;
				std::optional<bool> auto_load;
				std::optional<std::int32_t> priority;
				if (!runtime || !read_optional_bool(*runtime, "enabled", enabled) || !read_optional_bool(*runtime, "auto_load", auto_load) || !read_optional_priority(*runtime, priority))
				{
					result.errors.push_back({path, "manifest has invalid runtime policy"});
					return std::nullopt;
				}

				manifest.has_explicit_load_phase = static_cast<bool>((*runtime)["load_phase"]);
				if (const auto entry_node = (*runtime)["entry"])
				{
					const auto entry = entry_node.value<std::string>();
					if (!entry || entry->empty())
					{
						result.errors.push_back({path, "'runtime.entry' must be a non-empty string"});
						return std::nullopt;
					}
					manifest.entry = std::filesystem::path(*entry);
				}
				if (const auto managed_node = (*runtime)["managed_entry"])
				{
					const auto managed_entry = managed_node.value<std::string>();
					if (!managed_entry || managed_entry->empty())
					{
						result.errors.push_back({path, "'runtime.managed_entry' must be a non-empty string"});
						return std::nullopt;
					}
					manifest.managed_entry = std::filesystem::path(*managed_entry);
				}
			}

			if (manifest.runtime.load_phase == config::ModLoadPhase::GlobalInit
			    && (!manifest.has_static_identity || !manifest.has_explicit_load_phase || !manifest.entry))
			{
				result.errors.push_back({path, "global_init requires name, runtime.load_phase, and runtime.entry"});
				return std::nullopt;
			}
			return manifest;
		}

		[[nodiscard]] bool is_contained(const std::filesystem::path& path, const std::filesystem::path& boundary)
		{
			const auto relative = path.lexically_relative(boundary);
			return !relative.empty() && !relative.is_absolute() && !relative.has_root_path() && std::ranges::none_of(relative, [](const auto& part) {
				return part == "..";
			});
		}

		[[nodiscard]] std::optional<std::filesystem::path> canonical_existing(const std::filesystem::path& path, ModCatalogResult& result, const std::string_view label)
		{
			std::error_code error;
			const auto canonical = std::filesystem::canonical(path, error);
			if (error)
			{
				result.errors.push_back({path, std::string("cannot canonicalize ") + std::string(label) + ": " + error.message()});
				return std::nullopt;
			}
			return canonical;
		}

		[[nodiscard]] std::optional<std::filesystem::path> canonical_descendant(const std::filesystem::path& path, const std::filesystem::path& boundary, ModCatalogResult& result, const std::string_view label)
		{
			const auto canonical = canonical_existing(path, result, label);
			if (!canonical)
				return std::nullopt;
			if (!is_contained(*canonical, boundary) || *canonical == boundary)
			{
				result.errors.push_back({path, std::string(label) + " resolves outside its catalog boundary"});
				return std::nullopt;
			}
			return canonical;
		}

		[[nodiscard]] bool safe_relative_entry(const std::filesystem::path& entry)
		{
			return !entry.empty() && !entry.is_absolute() && !entry.has_root_path() && std::ranges::none_of(entry, [](const auto& part) {
				return part == "..";
			});
		}

		[[nodiscard]] bool is_native_library(const std::filesystem::path& path)
		{
			const auto extension = path.extension().string();
			return std::ranges::contains(kNativeModExtensions, extension);
		}

		[[nodiscard]] NativeSelection select_native_entry(const std::filesystem::path& root, const std::optional<std::filesystem::path>& entry, const config::ModLoadPhase load_phase, ModCatalogResult& result)
		{
			const auto native_path = root / mod_kind_folder_name(ModKind::Native);
			std::error_code error;
			const bool has_native = std::filesystem::is_directory(native_path, error);
			if (error && !is_missing(error))
			{
				result.errors.push_back({native_path, "cannot inspect native directory: " + error.message()});
				return {.valid = false};
			}
			if (!has_native)
			{
				if (entry || load_phase == config::ModLoadPhase::GlobalInit)
					result.errors.push_back({root / "mod.toml", "selected native directory is missing"});
				return {.valid = !entry && load_phase != config::ModLoadPhase::GlobalInit};
			}

			const auto native_root = canonical_descendant(native_path, root, result, "native directory");
			if (!native_root)
				return {.valid = false};

			if (entry)
			{
				if (!safe_relative_entry(*entry))
				{
					result.errors.push_back({root / "mod.toml", "runtime.entry must stay under the native directory"});
					return {.valid = false};
				}
				const auto selected = *native_root / *entry;
				if (!std::filesystem::is_regular_file(selected, error) || error || !is_native_library(selected))
				{
					result.errors.push_back({root / "mod.toml", "runtime.entry does not select a native library"});
					return {.valid = false};
				}
				const auto canonical = canonical_descendant(selected, *native_root, result, "native entry");
				return canonical ? NativeSelection{*canonical, true} : NativeSelection{.valid = false};
			}

			std::vector<std::filesystem::path> candidates;
			std::filesystem::directory_iterator it(*native_root, error);
			const std::filesystem::directory_iterator end;
			for (; !error && it != end; it.increment(error))
			{
				std::error_code entry_error;
				if (!it->is_regular_file(entry_error) || entry_error || !is_native_library(it->path()))
					continue;
				const auto canonical = canonical_descendant(it->path(), *native_root, result, "native candidate");
				if (!canonical)
					return {.valid = false};
				candidates.push_back(*canonical);
			}
			if (error)
			{
				result.errors.push_back({*native_root, "cannot enumerate native directory: " + error.message()});
				return {.valid = false};
			}
			if (candidates.size() > 1)
			{
				result.errors.push_back({*native_root, "multiple native libraries require runtime.entry"});
				return {.valid = false};
			}
			return {candidates.empty() ? std::nullopt : std::optional{candidates.front()}, true};
		}

		[[nodiscard]] std::optional<std::vector<std::filesystem::path>> collect_dotnet_entries(const std::filesystem::path& root, const std::optional<std::filesystem::path>& selected_entry, ModCatalogResult& result)
		{
			const auto dotnet_path = root / mod_kind_folder_name(ModKind::Dotnet);
			std::error_code error;
			const bool has_dotnet = std::filesystem::is_directory(dotnet_path, error);
			if (error && !is_missing(error))
			{
				result.errors.push_back({dotnet_path, "cannot inspect dotnet directory: " + error.message()});
				return std::nullopt;
			}
			if (!has_dotnet)
			{
				if (selected_entry)
				{
					result.errors.push_back({root / "mod.toml", "selected dotnet directory is missing"});
					return std::nullopt;
				}
				return std::vector<std::filesystem::path>{};
			}

			const auto dotnet_root = canonical_descendant(dotnet_path, root, result, "dotnet directory");
			if (!dotnet_root)
				return std::nullopt;
			if (selected_entry)
			{
				if (!safe_relative_entry(*selected_entry))
				{
					result.errors.push_back({root / "mod.toml", "runtime.managed_entry must stay under the dotnet directory"});
					return std::nullopt;
				}
				const auto selected = *dotnet_root / *selected_entry;
				if (!std::filesystem::is_regular_file(selected, error) || error || selected.extension() != ".dll")
				{
					result.errors.push_back({root / "mod.toml", "runtime.managed_entry does not select a managed library"});
					return std::nullopt;
				}
				const auto canonical = canonical_descendant(selected, *dotnet_root, result, "managed entry");
				return canonical ? std::optional{std::vector<std::filesystem::path>{*canonical}} : std::nullopt;
			}
			std::vector<std::filesystem::path> entries;
			std::filesystem::directory_iterator it(*dotnet_root, error);
			const std::filesystem::directory_iterator end;
			for (; !error && it != end; it.increment(error))
			{
				std::error_code entry_error;
				if (!it->is_regular_file(entry_error) || entry_error || it->path().extension() != ".dll")
					continue;
				const auto canonical = canonical_descendant(it->path(), *dotnet_root, result, "dotnet entry");
				if (!canonical)
					return std::nullopt;
				entries.push_back(*canonical);
			}
			if (error)
			{
				result.errors.push_back({*dotnet_root, "cannot enumerate dotnet directory: " + error.message()});
				return std::nullopt;
			}
			std::ranges::sort(entries, {}, [](const auto& path) {
				return path.generic_string();
			});
			return entries;
		}

		[[nodiscard]] const PolicyOverride* matching_policy(const std::vector<PolicyOverride>& policies, const std::string_view folder_id, const std::optional<std::string_view> static_name)
		{
			const auto explicit_match = std::ranges::find_if(policies, [folder_id](const PolicyOverride& policy) {
				return policy.id && *policy.id == folder_id;
			});
			if (explicit_match != policies.end())
				return &*explicit_match;
			if (!static_name)
				return nullptr;
			const auto legacy_match = std::ranges::find_if(policies, [static_name](const PolicyOverride& policy) {
				return !policy.id && policy.name && *policy.name == *static_name;
			});
			return legacy_match == policies.end() ? nullptr : &*legacy_match;
		}
	} // namespace

	ModCatalogResult discover_mods(const std::filesystem::path& loader_root)
	{
		ModCatalogResult result;
		const auto canonical_loader = canonical_existing(loader_root, result, "loader root");
		if (!canonical_loader)
			return result;
		const auto policies = load_policies(*canonical_loader, result);

		const auto mods_path = *canonical_loader / "mods";
		std::error_code error;
		if (!std::filesystem::is_directory(mods_path, error))
		{
			if (error && !is_missing(error))
				result.errors.push_back({mods_path, "cannot inspect mods directory: " + error.message()});
			return result;
		}
		const auto mods_root = canonical_descendant(mods_path, *canonical_loader, result, "mods directory");
		if (!mods_root)
			return result;

		std::filesystem::directory_iterator it(*mods_root, error);
		const std::filesystem::directory_iterator end;
		for (; !error && it != end; it.increment(error))
		{
			std::error_code entry_error;
			if (!it->is_directory(entry_error) || entry_error)
				continue;

			const auto folder_id = it->path().filename().string();
			const auto root = canonical_descendant(it->path(), *mods_root, result, "mod root");
			if (!root)
				continue;
			const auto manifest_path = *root / "mod.toml";
			const bool has_manifest = std::filesystem::is_regular_file(manifest_path, entry_error) && !entry_error;

			ManifestData manifest;
			if (has_manifest)
			{
				const auto loaded = load_manifest(manifest_path, result);
				if (!loaded)
					continue;
				manifest = *loaded;
			}

			const auto policy = matching_policy(policies,
			    folder_id,
			    manifest.has_static_identity ? std::optional<std::string_view>{manifest.name} : std::nullopt);
			if (policy && !policy->valid)
				continue;
			if (policy)
			{
				if (policy->enabled)
					manifest.runtime.enabled = *policy->enabled;
				if (policy->auto_load)
					manifest.runtime.auto_load = *policy->auto_load;
				if (policy->priority)
					manifest.runtime.priority = *policy->priority;
			}

			const auto native = select_native_entry(*root, manifest.entry, manifest.runtime.load_phase, result);
			if (!native.valid)
				continue;
			const auto dotnet = collect_dotnet_entries(*root, manifest.managed_entry, result);
			if (!dotnet)
				continue;

			result.mods.push_back({folder_id,
			    *root,
			    native.entry,
			    *dotnet,
			    manifest.has_static_identity ? manifest.name : folder_id,
			    manifest.runtime.priority,
			    manifest.runtime.enabled,
			    manifest.runtime.auto_load,
			    manifest.runtime.load_phase});
		}
		if (error)
			result.errors.push_back({*mods_root, "cannot enumerate mods directory: " + error.message()});

		std::ranges::sort(result.mods, [](const ModDefinition& lhs, const ModDefinition& rhs) {
			if (lhs.priority != rhs.priority)
				return lhs.priority > rhs.priority;
			return lhs.root.generic_string() < rhs.root.generic_string();
		});

		// Two folders declaring the same identity are the same mod twice -- a backup copy left in
		// mods/ is the usual way it happens -- and loading both puts two instances on the same
		// windows, hooks and engine state. Sorted order decides who keeps the name; the other is
		// refused by path, so the log says which folder to move out.
		std::vector<ModDefinition> unique_mods;
		unique_mods.reserve(result.mods.size());
		std::vector<std::string_view> claimed;
		claimed.reserve(result.mods.size());
		for (auto& mod : result.mods)
		{
			if (std::ranges::find(claimed, std::string_view{mod.name}) != claimed.end())
			{
				result.errors.push_back({mod.root, "another mod folder already claims the identity '" + mod.name + "'; move this one out of mods/"});
				continue;
			}
			unique_mods.push_back(std::move(mod));
			claimed.emplace_back(unique_mods.back().name);
		}
		result.mods = std::move(unique_mods);
		return result;
	}
} // namespace rml
