#include "RobloxModLoader/qt/dock_panel.hpp"

#include "RobloxModLoader/internal/common.hpp"
#include "RobloxModLoader/qt/qapplication.hpp"
#include "RobloxModLoader/qt/qicon.hpp"
#include "RobloxModLoader/qt/qstring.hpp"
#include "RobloxModLoader/qt/qt_module.hpp"

RML_LOG_SCOPE("Qt");

namespace rml::qt
{
	namespace
	{
		// Studio docks its own panels with QtitanDocking, so a mod panel must join
		// that manager instead of creating a competing QDockWidget.
		constexpr const char* kDockMainWindow = "Qtitan::DockMainWindow";
		constexpr const char* kDockPanelBase = "Qtitan::DockPanelBase";
	}

	void DockPanel::set_widget(QWidget* content)
	{
		static const auto fn = detail::docking<void (*)(void*, void*)>("Qtitan::DockWidgetPanel::setWidget(QWidget*)");
		if (fn && content)
			fn(this, content);
	}

	QWidget* DockPanel::widget() const
	{
		static const auto fn = detail::docking<void* (*)(const void*)>("Qtitan::DockWidgetPanel::widget() const");
		return fn ? static_cast<QWidget*>(fn(this)) : nullptr;
	}

	void DockPanel::set_caption(const std::string_view caption)
	{
		static const auto fn = detail::docking<void (*)(void*, const void*)>("Qtitan::DockWidgetPanel::setCaption(QString const&)");
		if (!fn)
			return;
		const QString title(caption);
		fn(this, title.data());
	}

	void DockPanel::set_icon(const QIcon& icon)
	{
		static const auto fn = detail::docking<void (*)(void*, const void*)>("Qtitan::DockWidgetPanel::setIcon(QIcon const&)");
		if (fn)
			fn(this, icon.data());
	}

	void DockPanel::set_visible(const bool visible)
	{
		static const auto fn = detail::docking<void (*)(void*, bool)>("Qtitan::DockWidgetPanel::setPanelVisible(bool)");
		if (fn)
			fn(this, visible);
	}

	void DockPanel::show_panel()
	{
		static const auto fn = detail::docking<void (*)(void*)>("Qtitan::DockWidgetPanel::showPanel()");
		if (fn)
			fn(this);
	}

	void DockPanel::activate()
	{
		static const auto fn = detail::docking<void (*)(void*)>("Qtitan::DockWidgetPanel::activate()");
		if (fn)
			fn(this);
	}

	bool DockPanel::is_closed() const
	{
		static const auto fn = detail::docking<bool (*)(const void*)>("Qtitan::DockWidgetPanel::isClosed() const");
		return fn ? fn(this) : true;
	}

	DockManager* DockManager::studio()
	{
		static DockManager* cached = nullptr;
		if (cached)
			return cached;

		static const auto manager_of_window = detail::docking_optional<void* (*)(const void*)>("Qtitan::DockMainWindow::dockPanelManager() const");
		// Studio's own panels know their manager, which works even when the main
		// window is a custom class the docking library does not name.
		static const auto manager_of_panel = detail::docking_optional<void* (*)(const void*)>("Qtitan::DockPanelBase::dockManager() const");
		if (!manager_of_window && !manager_of_panel)
		{
			RML_WARN("QtitanDocking is unavailable; dock panels are disabled");
			return nullptr;
		}

		for (QWidget* widget : QApplication::all_widgets())
		{
			if (!widget)
				continue;
			void* manager = nullptr;
			if (manager_of_window && widget->inherits(kDockMainWindow))
				manager = manager_of_window(widget);
			else if (manager_of_panel && widget->inherits(kDockPanelBase))
				manager = manager_of_panel(widget);
			if (manager)
			{
				RML_INFO("Docking manager {} taken from widget class '{}'", manager, widget->class_name());
				cached = static_cast<DockManager*>(manager);
				return cached;
			}
		}

		RML_WARN("No QtitanDocking manager found among {} widgets", QApplication::all_widgets().size());
		return nullptr;
	}

	DockPanel* DockManager::add_panel(const std::string_view caption, const DockArea area)
	{
		static const auto fn = detail::docking<void* (*)(void*, const void*, int, void*)>("Qtitan::DockPanelManager::addDockPanel(QString const&, Qtitan::DockPanelArea, Qtitan::DockPanelBase*)");
		if (!fn)
			return nullptr;
		const QString title(caption);
		return static_cast<DockPanel*>(fn(this, title.data(), static_cast<int>(area), nullptr));
	}

	void DockManager::remove_panel(DockPanel* panel)
	{
		static const auto fn = detail::docking<void (*)(void*, void*)>("Qtitan::DockPanelManager::removeDockPanel(Qtitan::DockWidgetPanel*)");
		if (fn && panel)
			fn(this, panel);
	}

	void DockManager::show_panel(DockPanel* panel, const bool focus)
	{
		static const auto fn = detail::docking<void (*)(void*, void*, bool, bool)>("Qtitan::DockPanelManager::showDockPanel(Qtitan::DockWidgetPanel*, bool, bool)");
		if (fn && panel)
			fn(this, panel, focus, true);
	}
}
