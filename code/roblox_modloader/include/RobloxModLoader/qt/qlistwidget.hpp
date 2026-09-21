#pragma once

#include "RobloxModLoader/qt/qt_owned.hpp"
#include "RobloxModLoader/qt/qwidget.hpp"

#include <functional>
#include <string_view>

namespace rml::qt
{
	class RML_EXPORT QListWidget : public QWidget
	{
	public:
		[[nodiscard]] static QListWidget* create(QWidget* parent);
		[[nodiscard]] static QtOwned<QListWidget> create_owned();

		static void destroy(QListWidget* list);

		void add_item(std::string_view text);
		void clear();
		[[nodiscard]] int current_row() const;
		void set_current_row(int row);
		[[nodiscard]] int count() const;

		/// Fires with the row the user selected, or -1 when the selection is cleared.
		void on_current_row_changed(std::function<void(int)> handler) const;
	};
}
