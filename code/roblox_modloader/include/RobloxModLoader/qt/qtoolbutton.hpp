#pragma once

#include "RobloxModLoader/qt/qabstractbutton.hpp"
#include "RobloxModLoader/qt/qt_owned.hpp"

namespace rml::qt
{
	/// The flat, icon-only button Studio uses in its own toolbars and in the
	/// command bar row.
	class RML_EXPORT QToolButton : public QAbstractButton
	{
	public:
		[[nodiscard]] static QToolButton* create(QWidget* parent);
		[[nodiscard]] static QtOwned<QToolButton> create_owned();

		static void destroy(QToolButton* button);

		/// Draws the frame only under the cursor, like the neighbouring buttons.
		void set_auto_raise(bool enabled);
	};
}
