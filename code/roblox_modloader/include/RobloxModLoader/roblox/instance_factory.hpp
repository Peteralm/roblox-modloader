#pragma once

#include "RobloxModLoader/rml_export.hpp"

namespace rml::roblox
{
	/// A `shared_ptr<Instance>` as the engine lays it out: the object, then the
	/// control block that counts references to it. The strong reference belongs
	/// to whoever received it and is given back with `release_instance` or
	/// `destroy_instance`.
	struct InstanceRef
	{
		void* instance{};
		void* control{};

		[[nodiscard]] explicit operator bool() const noexcept
		{
			return instance != nullptr;
		}
	};

	/// The roles the engine knows; `Scripting` is what `Instance.new` uses.
	enum class CreatorRole : int
	{
		Replication = 0,
		Serialization = 1,
		Scripting = 2,
		Engine = 3,
	};

	/// Creates an instance of a class by name, the same call the engine makes
	/// for `Instance.new`. Empty on an unknown class or a missing seam.
	[[nodiscard]] RML_EXPORT InstanceRef create_instance(const char* class_name,
	                                                     CreatorRole role = CreatorRole::Scripting) noexcept;

	/// The instance's class name, read from its own descriptor. Empty when the
	/// pointer does not look like an instance.
	[[nodiscard]] RML_EXPORT const char* class_name_of(void* instance) noexcept;


	/// Reparents an instance through the engine's own `Parent` property, so
	/// every ancestry notification the engine sends for a real reparent is
	/// sent for this one too. A null parent detaches it.
	RML_EXPORT bool set_parent(void* instance, void* parent) noexcept;

	/// Renames an instance through its `Name` property.
	RML_EXPORT bool set_name(void* instance, const char* name) noexcept;
	/// Runs the engine's own `Destroy`, reached through the reflection member of
	/// that name rather than a vtable slot, then lets go of the strong
	/// reference. False when the class has no such member.
	RML_EXPORT bool destroy_instance(InstanceRef reference) noexcept;

	/// Lets go of the strong reference without destroying anything.
	RML_EXPORT void release_instance(InstanceRef reference) noexcept;
} // namespace rml::roblox
