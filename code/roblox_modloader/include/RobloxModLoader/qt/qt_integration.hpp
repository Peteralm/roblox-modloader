#pragma once

#include "RobloxModLoader/qt/action_dispatcher.hpp"
#include "RobloxModLoader/qt/mods_menu.hpp"
#include "RobloxModLoader/qt/qt_owned.hpp"
#include "RobloxModLoader/qt/qtimer.hpp"
#include "RobloxModLoader/rml_export.hpp"

#include <atomic>
#include <functional>
#include <mutex>
#include <vector>

namespace rml::qt
{
	class RML_EXPORT QtIntegration
	{
	public:
		QtIntegration();

		~QtIntegration();

		QtIntegration(const QtIntegration&) = delete;

		QtIntegration& operator=(const QtIntegration&) = delete;

		bool ensure_action_hook();

		[[nodiscard]] bool is_action_hook_ready() const;

		[[nodiscard]] ModsMenu& menu();

		void on_menu_bar_built(QMenuBar* menu_bar);

		/// Starts the GUI-thread pump without waiting for Studio to build its menu
		/// bar, and adopts a menu bar that was built before the hook existed. Safe
		/// from any thread and idempotent.
		bool ensure_gui_pump();

		/// True once the pump is running; the retry loop stops calling ensure.
		[[nodiscard]] bool is_gui_pump_running() const;

		void on_action_triggered(QAction* action) const;

		void run_on_gui_thread(std::function<void()> task);

		[[nodiscard]] static QtIntegration* instance();

	private:
		void start_dispatch_timer();
		void adopt_existing_menu_bar();
		void drain_tasks();

		ActionDispatcher m_dispatcher;
		ModsMenu m_menu;

		std::mutex m_tasks_mutex;
		std::vector<std::function<void()>> m_tasks;
		QtOwned<QTimer> m_dispatch_timer;
		std::atomic_bool m_menu_bar_known{};

		static QtIntegration* s_instance;
	};
}
