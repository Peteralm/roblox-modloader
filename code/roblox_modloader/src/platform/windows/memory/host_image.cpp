#include "RobloxModLoader/internal/common.hpp"
#include "RobloxModLoader/platform/memory/host_image.hpp"

namespace rml::platform
{
	std::filesystem::path module_path_containing(const void* address)
	{
		HMODULE module_handle = nullptr;

		if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		        reinterpret_cast<LPCWSTR>(address), &module_handle))
			return {};

		wchar_t module_path[MAX_PATH];
		if (GetModuleFileNameW(module_handle, module_path, MAX_PATH) == 0)
			return {};

		return module_path;
	}

	std::filesystem::path executable_path()
	{
		wchar_t exe_path[MAX_PATH];

		if (GetModuleFileNameW(nullptr, exe_path, MAX_PATH) == 0)
			return {};

		return exe_path;
	}

	std::string_view studio_image_name()
	{
		return "RobloxStudioBeta.exe";
	}

	std::uintptr_t studio_preferred_image_base()
	{
		return 0x140000000;
	}

	namespace
	{
		struct WindowSearch
		{
			DWORD process_id;
			HWND found;
		};

		BOOL CALLBACK pick_process_window(HWND window, const LPARAM parameter)
		{
			auto* search = reinterpret_cast<WindowSearch*>(parameter);

			DWORD owner = 0;
			GetWindowThreadProcessId(window, &owner);

			if (owner != search->process_id || !IsWindowVisible(window) || GetWindow(window, GW_OWNER) != nullptr)
				return TRUE;

			search->found = window;
			return FALSE;
		}
	}

	void* acquire_main_window()
	{
		// Studio's window is the one we want, and it is not always the desktop's foreground window:
		// a Studio started in the background, or one launched by a tool, never takes focus. Asking
		// the OS for our own top-level window answers the question that was actually being asked.
		WindowSearch search{GetCurrentProcessId(), nullptr};

		for (int attempt = 0; attempt < 100 && !search.found; ++attempt)
		{
			EnumWindows(&pick_process_window, reinterpret_cast<LPARAM>(&search));

			if (!search.found)
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}

		if (!search.found)
			throw std::runtime_error("Failed to find Roblox Studio window: this process has no visible top-level window");

		return search.found;
	}
}
