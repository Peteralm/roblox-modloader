#include "RobloxModLoader/platform/core/early_phase.hpp"

namespace rml::platform
{
	// macOS has no global-init window yet: nothing runs before the loader thread,
	// so a mod that asked for that phase is reported as unsupported instead of
	// silently loading late.
	bool global_init_phase_completed() noexcept
	{
		return false;
	}
} // namespace rml::platform
