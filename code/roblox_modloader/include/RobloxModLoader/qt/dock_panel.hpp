#pragma once

#include "RobloxModLoader/qt/qwidget.hpp"
#include "RobloxModLoader/rml_export.hpp"

#include <string_view>

namespace rml::qt
{
	class QIcon;

	/// Where a panel lands. The values match Qt's own dock areas, which is what
	/// QtitanDocking accepts.
	enum class DockArea : int
	{
		Left = 0x1,
		Right = 0x2,
		Top = 0x4,
		Bottom = 0x8,
		/// Tabbed onto the target panel instead of beside it.
		Inside = 0x10,
	};

	/// A dockable panel of Studio's own docking system: it drags, tabs, floats and
	/// is restored with the rest of the layout, exactly like Explorer or Output.
	class RML_EXPORT DockPanel : public QWidget
	{
	public:
		void set_widget(QWidget* content);
		[[nodiscard]] QWidget* widget() const;
		void set_caption(std::string_view caption);
		void set_icon(const QIcon& icon);
		void set_visible(bool visible);
		void show_panel();
		void activate();
		[[nodiscard]] bool is_closed() const;
	};

	/// Studio's docking manager. Panels are added to the manager Studio already
	/// owns, so a mod's panel shares the layout with the built-in ones.
	class RML_EXPORT DockManager
	{
	public:
		/// The manager of Studio's main window, or nullptr when the docking library
		/// or the main window cannot be resolved.
		[[nodiscard]] static DockManager* studio();

		[[nodiscard]] DockPanel* add_panel(std::string_view caption, DockArea area);
		void remove_panel(DockPanel* panel);
		void show_panel(DockPanel* panel, bool focus = true);
	};
}
