#include "RobloxModLoader/qt/qt_integration.hpp"

#include "RobloxModLoader/qt/qapplication.hpp"
#include "RobloxModLoader/qt/qwidget.hpp"
#include "RobloxModLoader/logger/logger.hpp"

RML_LOG_SCOPE("QtIntegration")

namespace rml::qt
{
	QtIntegration* QtIntegration::s_instance = nullptr;

	QtIntegration::QtIntegration() :
	    m_menu(m_dispatcher)
	{
		s_instance = this;
	}

	QtIntegration::~QtIntegration()
	{
		s_instance = nullptr;
	}

	ModsMenu& QtIntegration::menu()
	{
		return m_menu;
	}

	void QtIntegration::on_menu_bar_built(QMenuBar* menu_bar)
	{
		m_menu_bar_known.store(true, std::memory_order_release);
		m_menu.rebuild(menu_bar);
		start_dispatch_timer();
	}

	bool QtIntegration::ensure_gui_pump()
	{
		if (m_dispatch_timer)
			return true;
		// Studio builds its menu bar once, at startup. When the loader arrives
		// after that moment the hook never fires, and without this the mods menu,
		// every queued GUI task and every panel button stay invisible forever.
		start_dispatch_timer();
		return static_cast<bool>(m_dispatch_timer);
	}

	void QtIntegration::start_dispatch_timer()
	{
		if (m_dispatch_timer)
			return;

		QApplication* application = QApplication::instance();
		void* const gui_thread = application ? application->owner_thread() : nullptr;
		if (!gui_thread)
			return;

		m_dispatch_timer = QTimer::create_owned();
		if (!m_dispatch_timer)
			return;

		m_dispatch_timer->setInterval(0);
		m_dispatch_timer->on_timeout([this] {
			if (!m_menu_bar_known.load(std::memory_order_acquire))
				adopt_existing_menu_bar();
			drain_tasks();
		});
		// The timer was built on whichever thread got here first; Qt only ticks it
		// from the thread that owns it, and only that thread may start it.
		m_dispatch_timer->move_to_thread(gui_thread);
		if (!m_dispatch_timer->invoke_queued("start"))
		{
			RML_WARN("Could not queue the GUI dispatch timer; menu and panels stay unavailable");
			m_dispatch_timer = {};
		}
	}

	void QtIntegration::adopt_existing_menu_bar()
	{
		for (QWidget* widget : QApplication::all_widgets())
		{
			if (!widget || !widget->inherits("QMenuBar"))
				continue;
			m_menu_bar_known.store(true, std::memory_order_release);
			RML_INFO("Adopted a menu bar that was built before the hook: '{}'", widget->class_name());
			m_menu.rebuild(reinterpret_cast<QMenuBar*>(widget));
			return;
		}
	}

	void QtIntegration::run_on_gui_thread(std::function<void()> task)
	{
		if (!task)
			return;

		const std::scoped_lock lock(m_tasks_mutex);
		m_tasks.push_back(std::move(task));
	}

	void QtIntegration::drain_tasks()
	{
		std::vector<std::function<void()>> pending;
		{
			const std::scoped_lock lock(m_tasks_mutex);
			pending.swap(m_tasks);
		}

		for (auto& task : pending)
		{
			try
			{
				task();
			}
			catch (const std::exception& error)
			{
				RML_ERROR("GUI task threw: {}", error.what());
			}
			catch (...)
			{
				RML_ERROR("GUI task threw an unknown exception");
			}
		}
	}

	void QtIntegration::on_action_triggered(QAction* action) const
	{
		m_dispatcher.dispatch(action);
	}

	bool QtIntegration::ensure_action_hook()
	{
		return m_dispatcher.ensure_hook();
	}

	bool QtIntegration::is_action_hook_ready() const
	{
		return m_dispatcher.is_hook_ready();
	}

	QtIntegration* QtIntegration::instance()
	{
		return s_instance;
	}
}
