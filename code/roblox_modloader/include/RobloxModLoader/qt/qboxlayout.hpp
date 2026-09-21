#pragma once

#include "RobloxModLoader/qt/qobject.hpp"

namespace rml::qt
{
	class QWidget;

	/// Stacks widgets top to bottom and resizes them with their parent, which is
	/// what makes a panel's contents follow the dock area instead of floating at a
	/// fixed position.
	class RML_EXPORT QVBoxLayout : public QObject
	{
	public:
		/// Creates the layout and installs it on `parent`.
		[[nodiscard]] static QVBoxLayout* create(QWidget* parent);

		void add_widget(QWidget* widget, int stretch = 0);
		void set_contents_margins(int left, int top, int right, int bottom);
		void set_spacing(int spacing);
	};
}
