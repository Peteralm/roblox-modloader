#pragma once

#include "RobloxModLoader/internal/platform.hpp"

#if defined(RML_WINDOWS)
	#define RML_ENGINE_CALL __fastcall
	#define RML_ENGINE_CLASS __declspec(empty_bases)
#else
	#define RML_ENGINE_CALL
	#define RML_ENGINE_CLASS
#endif
