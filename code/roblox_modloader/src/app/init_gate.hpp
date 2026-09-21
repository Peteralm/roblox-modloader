#pragma once

#include "RobloxModLoader/core/init_latch.hpp"
#include "RobloxModLoader/hooking/i_hook_engine.hpp"

#include <atomic>
#include <expected>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace rml
{
	class InitContext;

	class InitGate
	{
	public:
		using Callback = std::function<void(InitContext&)>;

		InitGate();
		~InitGate();

		static InitGate* instance();

		[[nodiscard]] std::expected<void, std::string> install();
		void register_callback(std::string mod_name, Callback callback);
		void mark_mods_loaded();
		void abort();
		[[nodiscard]] InitGateState state() const;
		[[nodiscard]] bool is_open() const;
		void on_global_init_reached();

	private:
		void run_callbacks();

		InitLatch m_latch;
		std::unique_ptr<IHookEngine> m_owned_engine;
		std::mutex m_callbacks_mutex;
		std::vector<std::pair<std::string, Callback>> m_callbacks;
		std::atomic<bool> m_open{false};
	};
}

inline rml::InitGate* g_init_gate{};
