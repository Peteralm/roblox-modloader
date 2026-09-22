#pragma once

namespace rml::platform
{
	// True once the platform ran the global-init phase to completion. Mods that
	// asked for that phase are already inside the process at that point, so the
	// mod manager only has to adopt them.
	[[nodiscard]] bool global_init_phase_completed() noexcept;

	// Why the phase ended where it did, in one line. Never null; a platform with
	// no global-init window says so instead of leaving the caller guessing.
	[[nodiscard]] const char* global_init_phase_diagnostic() noexcept;

	// Blocks until the global-init phase settles, at most timeout_ms. The loader
	// runs on its own thread and reaches the mod manager while Studio is still
	// starting, so sampling the phase once loses a race the mods cannot recover
	// from. Returns what global_init_phase_completed() would return afterwards.
	[[nodiscard]] bool wait_for_global_init_phase(unsigned timeout_ms) noexcept;
}
