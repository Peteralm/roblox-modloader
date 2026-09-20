#include "mod_catalog.hpp"

#include "RobloxModLoader/config/config_serialization.hpp"

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
	};

	struct ManifestData
	{
		std::string name;
		config::ModConfig::Runtime runtime;
		std::optional<std::filesystem::path> entry;
		bool has_static_identity{};
		bool has_explicit_load_phase{};
	};

	[[nodiscard]] std::filesystem::path canonical_path(const std::filesystem::path& path)
	{
		std::error_code error;
		const auto canonical = std::filesystem::weakly_canonical(path, error);
		return error ? path.lexically_normal() : canonical;
	}

	[[nodiscard]] std::optional<toml::table> read_toml(
	    const std::filesystem::path& path, ModCatalogResult& result)
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

	[[nodiscard]] bool read_optional_string(const toml::table& table, const std::string_view key,
	    std::optional<std::string>& value)
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

	[[nodiscard]] bool read_optional_bool(
	    const toml::table& table, const std::string_view key, std::optional<bool>& value)
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
		if (!parsed || *parsed < std::numeric_limits<std::int32_t>::min()
		    || *parsed > std::numeric_limits<std::int32_t>::max())
			return false;
		value = static_cast<std::int32_t>(*parsed);
		return true;
	}

	[[nodiscard]] std::vector<PolicyOverride> load_policies(
	    const std::filesystem::path& loader_root, ModCatalogResult& result)
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
					result.errors.push_back({path, "mods[" + std::to_string(index) + "] has invalid runtime policy"});
					continue;
				}
			}

			policies.push_back(std::move(policy));
		}
		return policies;
	}

	[[nodiscard]] std::optional<ManifestData> load_manifest(
	    const std::filesystem::path& path, ModCatalogResult& result)
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
			if (!runtime)
			{
				result.errors.push_back({path, "'runtime' must be a table"});
				return std::nullopt;
			}

			std::optional<bool> enabled;
			std::optional<bool> auto_load;
			std::optional<std::int32_t> priority;
			if (!read_optional_bool(*runtime, "enabled", enabled)
			    || !read_optional_bool(*runtime, "auto_load", auto_load)
			    || !read_optional_priority(*runtime, priority))
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
		}

		if (manifest.runtime.load_phase == config::ModLoadPhase::GlobalInit
		    && (!manifest.has_static_identity || !manifest.has_explicit_load_phase || !manifest.entry))
		{
			result.errors.push_back({path, "global_init requires name, runtime.load_phase, and runtime.entry"});
			return std::nullopt;
		}
		return manifest;
	}

	[[nodiscard]] bool is_native_library(const std::filesystem::path& path)
	{
		const auto extension = path.extension().string();
		return extension == ".dll" || extension == ".so" || extension == ".dylib";
	}

	[[nodiscard]] bool safe_relative_entry(const std::filesystem::path& entry)
	{
		if (entry.empty() || entry.is_absolute() || entry.has_root_path())
			return false;
		return std::ranges::none_of(entry, [](const auto& part) { return part == ".."; });
	}

	[[nodiscard]] std::optional<std::filesystem::path> select_native_entry(const std::filesystem::path& root,
	    const std::optional<std::filesystem::path>& entry, const config::ModLoadPhase load_phase,
	    ModCatalogResult& result)
	{
		const auto native_root = root / "native";
		if (entry)
		{
			if (!safe_relative_entry(*entry))
			{
				result.errors.push_back({root / "mod.toml", "runtime.entry must stay under the native directory"});
				return std::nullopt;
			}

			const auto selected = native_root / *entry;
			std::error_code error;
			if (!std::filesystem::is_regular_file(selected, error) || error || !is_native_library(selected))
			{
				result.errors.push_back({root / "mod.toml", "runtime.entry does not select a native library"});
				return std::nullopt;
			}

			const auto canonical = canonical_path(selected);
			const auto relative = canonical.lexically_relative(canonical_path(native_root));
			if (!safe_relative_entry(relative))
			{
				result.errors.push_back({root / "mod.toml", "runtime.entry resolves outside the native directory"});
				return std::nullopt;
			}
			return canonical;
		}

		std::error_code error;
		if (!std::filesystem::is_directory(native_root, error) || error)
		{
			if (load_phase == config::ModLoadPhase::GlobalInit)
				result.errors.push_back({root / "mod.toml", "global_init native directory is missing"});
			return std::nullopt;
		}

		std::vector<std::filesystem::path> candidates;
		std::filesystem::directory_iterator it(native_root, error);
		const std::filesystem::directory_iterator end;
		for (; !error && it != end; it.increment(error))
		{
			std::error_code entry_error;
			if (it->is_regular_file(entry_error) && !entry_error && is_native_library(it->path()))
				candidates.push_back(canonical_path(it->path()));
		}
		if (error)
		{
			result.errors.push_back({native_root, "cannot enumerate native directory: " + error.message()});
			return std::nullopt;
		}

		if (candidates.empty())
			return std::nullopt;
		if (candidates.size() != 1)
		{
			result.errors.push_back({native_root, "multiple native libraries require runtime.entry"});
			return std::nullopt;
		}
		return std::move(candidates.front());
	}

	[[nodiscard]] const PolicyOverride* matching_policy(const std::vector<PolicyOverride>& policies,
	    const std::string_view folder_id, const std::optional<std::string_view> static_name)
	{
		const auto explicit_match = std::ranges::find_if(
		    policies, [folder_id](const PolicyOverride& policy) { return policy.id && *policy.id == folder_id; });
		if (explicit_match != policies.end())
			return &*explicit_match;
		if (!static_name)
			return nullptr;
		const auto legacy_match = std::ranges::find_if(policies,
		    [static_name](const PolicyOverride& policy) { return !policy.id && policy.name && *policy.name == *static_name; });
		return legacy_match == policies.end() ? nullptr : &*legacy_match;
	}

	[[nodiscard]] std::string path_sort_key(const std::filesystem::path& path)
	{
		return path.generic_string();
	}
} // namespace

	ModCatalogResult discover_native_mods(const std::filesystem::path& loader_root)
	{
		ModCatalogResult result;
		const auto policies = load_policies(loader_root, result);
		const auto mods_root = loader_root / "mods";

		std::error_code error;
		if (!std::filesystem::is_directory(mods_root, error))
		{
			if (error)
				result.errors.push_back({mods_root, "cannot inspect mods directory: " + error.message()});
			return result;
		}

		std::filesystem::directory_iterator it(mods_root, error);
		const std::filesystem::directory_iterator end;
		for (; !error && it != end; it.increment(error))
		{
			std::error_code entry_error;
			if (!it->is_directory(entry_error) || entry_error)
				continue;

			const auto folder_id = it->path().filename().string();
			const auto root = canonical_path(it->path());
			const auto manifest_path = root / "mod.toml";
			const bool has_manifest = std::filesystem::is_regular_file(manifest_path, entry_error) && !entry_error;

			ManifestData manifest;
			if (has_manifest)
			{
				const auto loaded = load_manifest(manifest_path, result);
				if (!loaded)
					continue;
				manifest = *loaded;
			}

			const auto policy = matching_policy(policies, folder_id,
			    manifest.has_static_identity ? std::optional<std::string_view>{manifest.name} : std::nullopt);
			if (policy)
			{
				if (policy->enabled)
					manifest.runtime.enabled = *policy->enabled;
				if (policy->auto_load)
					manifest.runtime.auto_load = *policy->auto_load;
				if (policy->priority)
					manifest.runtime.priority = *policy->priority;
			}

			const auto error_count = result.errors.size();
			const auto dll = select_native_entry(root, manifest.entry, manifest.runtime.load_phase, result);
			if (result.errors.size() != error_count)
				continue;

			const auto name = manifest.has_static_identity ? manifest.name : folder_id;
			result.roots.push_back({folder_id, root, name, manifest.runtime.priority, manifest.runtime.enabled,
			    manifest.runtime.auto_load});
			if (dll)
			{
				result.native_mods.push_back({folder_id, root, *dll, name, manifest.runtime.priority,
				    manifest.runtime.enabled, manifest.runtime.auto_load, manifest.runtime.load_phase});
			}
		}
		if (error)
			result.errors.push_back({mods_root, "cannot enumerate mods directory: " + error.message()});

		const auto root_less = [](const ModRootDefinition& lhs, const ModRootDefinition& rhs) {
			if (lhs.priority != rhs.priority)
				return lhs.priority > rhs.priority;
			return path_sort_key(lhs.root) < path_sort_key(rhs.root);
		};
		const auto native_less = [](const NativeModDefinition& lhs, const NativeModDefinition& rhs) {
			if (lhs.priority != rhs.priority)
				return lhs.priority > rhs.priority;
			return path_sort_key(lhs.dll) < path_sort_key(rhs.dll);
		};
		std::ranges::sort(result.roots, root_less);
		std::ranges::sort(result.native_mods, native_less);
		return result;
	}
} // namespace rml
