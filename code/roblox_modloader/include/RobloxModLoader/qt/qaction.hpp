#pragma once

#include "RobloxModLoader/qt/qobject.hpp"
#include "RobloxModLoader/rml_export.hpp"

#include <string>

namespace rml::qt
{
	class QMenu;
	class QIcon;

	class RML_EXPORT QAction : public QObject
	{
	public:
		enum ActionEvent
		{
			Trigger = 0,
			Hover = 1,
		};
		
		enum MenuRole
		{
			NoRole = 0,
			TextHeuristicRole = 1,
			ApplicationSpecificRole = 2,
			AboutQtRole = 3,
			AboutRole = 4,
			PreferencesRole = 5,
			QuitRole = 6,
		};

		void setCheckable(bool checkable);
		void setChecked(bool checked);
		[[nodiscard]] bool isChecked() const;
		void setIcon(const QIcon& icon);
		void setMenuRole(MenuRole role);

		/// The label the user sees, already UTF-8. Studio's own menu entries are
		/// the only way a mod can find a command it did not create.
		[[nodiscard]] std::string text() const;
		/// The submenu this action opens, or nullptr for a plain command.
		[[nodiscard]] QMenu* menu() const;
		/// Runs the command as if the user had picked it from the menu.
		void trigger();

		[[nodiscard]] static void* activate_address();
	};
}
