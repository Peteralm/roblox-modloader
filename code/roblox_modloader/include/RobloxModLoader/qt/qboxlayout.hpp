#pragma once

#include "RobloxModLoader/qt/qobject.hpp"

namespace rml::qt
{
	class QWidget;

	/// Stacks widgets and resizes them with their parent, which is what makes a
	/// panel's contents follow the dock area instead of floating at a fixed
	/// position. Also the handle used to add a widget to a layout Studio built.
	class RML_EXPORT QBoxLayout : public QObject
	{
	public:
		void add_widget(QWidget* widget, int stretch = 0);
		/// Negative or out-of-range index appends, like Qt itself.
		void insert_widget(int index, QWidget* widget, int stretch = 0);
		[[nodiscard]] int count() const;
		void set_contents_margins(int left, int top, int right, int bottom);
		void set_spacing(int spacing);
	};

	class RML_EXPORT QVBoxLayout : public QBoxLayout
	{
	public:
		/// Creates the layout and installs it on `parent`.
		[[nodiscard]] static QVBoxLayout* create(QWidget* parent);
	};

	class RML_EXPORT QHBoxLayout : public QBoxLayout
	{
	public:
		[[nodiscard]] static QHBoxLayout* create(QWidget* parent);
	};
}
