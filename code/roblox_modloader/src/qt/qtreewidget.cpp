#include "RobloxModLoader/qt/qtreewidget.hpp"

#include "RobloxModLoader/qt/qstring.hpp"
#include "RobloxModLoader/qt/qt_module.hpp"
#include "qt_connect.hpp"

#include <utility>

namespace rml::qt
{
	namespace
	{
		// QTreeWidgetItem is a plain heap object, not a QWidget; Qt 5 lays it out
		// in well under 128 bytes, the same budget the other widgets use here.
		constexpr int kNoItemType = 0;
		constexpr int kSelectRows = 1; // QAbstractItemView::SelectRows
	}

	QTreeWidgetItem* QTreeWidgetItem::create()
	{
		static const auto construct = detail::widgets<void* (*)(void*, int)>(
		    "QTreeWidgetItem::QTreeWidgetItem(int)");
		return detail::heap_construct<QTreeWidgetItem>(detail::WIDGET_INSTANCE_SIZE, construct, kNoItemType);
	}

	void QTreeWidgetItem::destroy(QTreeWidgetItem* item)
	{
		static const auto dtor = detail::widgets<void (*)(void*)>("QTreeWidgetItem::~QTreeWidgetItem()");
		detail::heap_destroy(dtor, item);
	}

	void QTreeWidgetItem::set_text(const int column, const std::string_view text)
	{
		static const auto fn = detail::widgets<void (*)(void*, int, const void*)>(
		    "QTreeWidgetItem::setText(int, QString const&)");
		if (!fn)
			return;
		const QString value(text);
		fn(this, column, value.data());
	}

	void QTreeWidgetItem::set_tool_tip(const int column, const std::string_view text)
	{
		static const auto fn = detail::widgets<void (*)(void*, int, const void*)>(
		    "QTreeWidgetItem::setToolTip(int, QString const&)");
		if (!fn)
			return;
		const QString value(text);
		fn(this, column, value.data());
	}

	QTreeWidget* QTreeWidget::create(QWidget* parent)
	{
		static const auto construct = detail::widgets<void* (*)(void*, void*)>(
		    "QTreeWidget::QTreeWidget(QWidget*)");
		return detail::heap_construct<QTreeWidget>(detail::WIDGET_INSTANCE_SIZE, construct, parent);
	}

	QtOwned<QTreeWidget> QTreeWidget::create_owned()
	{
		return QtOwned<QTreeWidget>(create(nullptr));
	}

	void QTreeWidget::destroy(QTreeWidget* tree)
	{
		static const auto dtor = detail::widgets<void (*)(void*)>("QTreeWidget::~QTreeWidget()");
		detail::heap_destroy(dtor, tree);
	}

	void QTreeWidget::set_column_count(const int count)
	{
		static const auto fn = detail::widgets<void (*)(void*, int)>("QTreeWidget::setColumnCount(int)");
		if (fn)
			fn(this, count);
	}

	void QTreeWidget::set_header_item(QTreeWidgetItem* item)
	{
		static const auto fn = detail::widgets<void (*)(void*, void*)>(
		    "QTreeWidget::setHeaderItem(QTreeWidgetItem*)");
		if (fn && item)
			fn(this, item);
	}

	void QTreeWidget::add_item(QTreeWidgetItem* item)
	{
		static const auto fn = detail::widgets<void (*)(void*, void*)>(
		    "QTreeWidget::addTopLevelItem(QTreeWidgetItem*)");
		if (fn && item)
			fn(this, item);
	}

	void QTreeWidget::clear()
	{
		static const auto fn = detail::widgets<void (*)(void*)>("QTreeWidget::clear()");
		if (fn)
			fn(this);
	}

	void QTreeWidget::set_root_decorated(const bool decorated)
	{
		static const auto fn = detail::widgets<void (*)(void*, bool)>("QTreeView::setRootIsDecorated(bool)");
		if (fn)
			fn(this, decorated);
	}

	void QTreeWidget::set_alternating_row_colors(const bool alternating)
	{
		static const auto fn = detail::widgets<void (*)(void*, bool)>(
		    "QAbstractItemView::setAlternatingRowColors(bool)");
		if (fn)
			fn(this, alternating);
	}

	void QTreeWidget::set_uniform_row_heights(const bool uniform)
	{
		static const auto fn = detail::widgets<void (*)(void*, bool)>("QTreeView::setUniformRowHeights(bool)");
		if (fn)
			fn(this, uniform);
	}

	void QTreeWidget::set_selects_whole_row(const bool whole_row)
	{
		static const auto fn = detail::widgets<void (*)(void*, int)>(
		    "QAbstractItemView::setSelectionBehavior(QAbstractItemView::SelectionBehavior)");
		if (fn)
			fn(this, whole_row ? kSelectRows : 0);
	}

	void QTreeWidget::set_column_width(const int column, const int width)
	{
		static const auto fn = detail::widgets<void (*)(void*, int, int)>("QTreeView::setColumnWidth(int, int)");
		if (fn)
			fn(this, column, width);
	}

	void QTreeWidget::stretch_last_column(const bool stretch)
	{
		static const auto header = detail::widgets<void* (*)(const void*)>("QTreeView::header() const");
		static const auto fn = detail::widgets<void (*)(void*, bool)>(
		    "QHeaderView::setStretchLastSection(bool)");
		if (!header || !fn)
			return;
		if (auto* view = header(this))
			fn(view, stretch);
	}

	int QTreeWidget::current_row() const
	{
		static const auto current = detail::widgets<void* (*)(const void*)>(
		    "QTreeWidget::currentItem() const");
		static const auto index_of = detail::widgets<int (*)(const void*, void*)>(
		    "QTreeWidget::indexOfTopLevelItem(QTreeWidgetItem*) const");
		if (!current || !index_of)
			return -1;
		auto* item = current(this);
		return item ? index_of(this, item) : -1;
	}

	void QTreeWidget::set_current_row(const int row)
	{
		static const auto top_level = detail::widgets<void* (*)(const void*, int)>(
		    "QTreeWidget::topLevelItem(int) const");
		static const auto set_current = detail::widgets<void (*)(void*, void*)>(
		    "QTreeWidget::setCurrentItem(QTreeWidgetItem*)");
		if (!top_level || !set_current)
			return;
		if (auto* item = top_level(this, row))
			set_current(this, item);
	}

	int QTreeWidget::count() const
	{
		static const auto fn = detail::widgets<int (*)(const void*)>("QTreeWidget::topLevelItemCount() const");
		return fn ? fn(this) : 0;
	}

	void QTreeWidget::on_current_row_changed(std::function<void(int)> handler) const
	{
		static void* const signal = detail::widgets_export(
		    "QTreeWidget::currentItemChanged(QTreeWidgetItem*, QTreeWidgetItem*)");
		static const void* const meta = detail::widgets_export("QTreeWidget::staticMetaObject");
		detail::connect_function(this, signal, meta, [this, handler = std::move(handler)](void**) {
			if (handler)
				handler(current_row());
		});
	}
}
