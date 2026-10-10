#include "RobloxModLoader/util/shell.hpp"

#include "RobloxModLoader/internal/common.hpp"
#include "RobloxModLoader/util/string.hpp"


#if defined(RML_WINDOWS)
	#include <shellapi.h>
	#pragma comment(lib, "shell32.lib")
	#pragma comment(lib, "user32.lib")
#else
	#include <spawn.h>
	#include <sys/wait.h>

	#include <thread>

extern char** environ;
#endif

namespace rml::utils
{
#if !defined(RML_WINDOWS)
	static void spawn_detached(const char* program, const std::string& argument)
	{
		std::string name(program);
		char* argv[] = {name.data(), const_cast<char*>(argument.c_str()), nullptr};
		pid_t pid = 0;
		if (posix_spawnp(&pid, program, nullptr, nullptr, argv, environ) == 0)
			std::thread([pid] {
				int status = 0;
				waitpid(pid, &status, 0);
			}).detach();
	}
#endif

	void shell::open(const std::filesystem::path& path)
	{
#if defined(RML_WINDOWS)
		ShellExecuteW(nullptr, L"open", path.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#elif defined(RML_MACOS)
		spawn_detached("open", path.string());
#else
		spawn_detached("xdg-open", path.string());
#endif
	}

	void shell::open_folder(const std::filesystem::path& path)
	{
		std::error_code ec;
		std::filesystem::create_directories(path, ec);
		open(path);
	}

	void shell::message_box(const std::string_view title, const std::string_view text)
	{
#if defined(RML_WINDOWS)
		MessageBoxW(nullptr, to_wide(text).c_str(), to_wide(title).c_str(), MB_OK | MB_ICONINFORMATION);
#else
		(void)title;
		(void)text;
#endif
	}
}
