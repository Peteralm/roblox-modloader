#pragma once

namespace rml::platform
{
	// True once the platform ran the global-init phase to completion. Mods that
	// asked for that phase are already inside the process at that point, so the
	// mod manager only has to adopt them.
	[[nodiscard]] bool global_init_phase_completed() noexcept;
}
