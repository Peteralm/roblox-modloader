#pragma once

#include "RobloxModLoader/qt/qt_owned.hpp"
#include "RobloxModLoader/qt/qwidget.hpp"

#include <functional>
#include <string_view>

namespace rml::qt
{
	/// One row. Columns are filled by index; the widget owns the item once it is
	/// added, so a row is never freed by the caller.
	class RML_EXPORT QTreeWidgetItem
	{
	public:
		[[nodiscard]] static QTreeWidgetItem* create();
		static void destroy(QTreeWidgetItem* item);

		void set_text(int column, std::string_view text);
		void set_tool_tip(int column, std::string_view text);
	};

	/// The multi-column list Studio uses for Scene Analysis and the like: it
	/// brings its own header, alternating rows and scrollbars.
	class RML_EXPORT QTreeWidget : public QWidget
	{
	public:
		[[nodiscard]] static QTreeWidget* create(QWidget* parent);
		[[nodiscard]] static QtOwned<QTreeWidget> create_owned();

		static void destroy(QTreeWidget* tree);

		void set_column_count(int count);
		/// Takes ownership; each column label is a column of this item.
		void set_header_item(QTreeWidgetItem* item);
		void add_item(QTreeWidgetItem* item);
		void clear();

		void set_root_decorated(bool decorated);
		void set_alternating_row_colors(bool alternating);
		void set_uniform_row_heights(bool uniform);
		void set_selects_whole_row(bool whole_row);
		void set_column_width(int column, int width);
		/// Lets the last column take the remaining width, like Studio's own lists.
		void stretch_last_column(bool stretch);

		[[nodiscard]] int current_row() const;
		void set_current_row(int row);
		/// The row at `row`, or nullptr when out of range. Rewriting a row in place
		/// is what keeps the scrollbar where the user left it.
		[[nodiscard]] QTreeWidgetItem* item(int row) const;

		/// Removes and frees the row at `row`.
		void remove_item(int row);

		[[nodiscard]] int count() const;

		/// Fires with the selected row index, or -1 when nothing is selected.
		void on_current_row_changed(std::function<void(int)> handler) const;
	};
}
