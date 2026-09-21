#include "RobloxModLoader/qt/qlistwidget.hpp"

#include "RobloxModLoader/qt/qstring.hpp"
#include "RobloxModLoader/qt/qt_module.hpp"
#include "qt_connect.hpp"

namespace rml::qt
{
	QListWidget* QListWidget::create(QWidget* parent)
	{
		static const auto construct = detail::widgets<void* (*)(void*, void*)>("QListWidget::QListWidget(QWidget*)");
		return detail::heap_construct<QListWidget>(detail::WIDGET_INSTANCE_SIZE, construct, parent);
	}

	QtOwned<QListWidget> QListWidget::create_owned()
	{
		return QtOwned<QListWidget>(create(nullptr));
	}

	void QListWidget::destroy(QListWidget* list)
	{
		static const auto dtor = detail::widgets<void (*)(void*)>("QListWidget::~QListWidget()");
		detail::heap_destroy(dtor, list);
	}

	void QListWidget::add_item(const std::string_view text)
	{
		static const auto fn = detail::widgets<void (*)(void*, const void*)>("QListWidget::addItem(QString const&)");
		if (!fn)
			return;
		const QString value(text);
		fn(this, value.data());
	}

	void QListWidget::clear()
	{
		static const auto fn = detail::widgets<void (*)(void*)>("QListWidget::clear()");
		if (fn)
			fn(this);
	}

	int QListWidget::current_row() const
	{
		static const auto fn = detail::widgets<int (*)(const void*)>("QListWidget::currentRow() const");
		return fn ? fn(this) : -1;
	}

	void QListWidget::set_current_row(const int row)
	{
		static const auto fn = detail::widgets<void (*)(void*, int)>("QListWidget::setCurrentRow(int)");
		if (fn)
			fn(this, row);
	}

	int QListWidget::count() const
	{
		static const auto fn = detail::widgets<int (*)(const void*)>("QListWidget::count() const");
		return fn ? fn(this) : 0;
	}

	void QListWidget::on_current_row_changed(std::function<void(int)> handler) const
	{
		static void* const signal = detail::widgets_export("QListWidget::currentRowChanged(int)");
		static const void* const meta = detail::widgets_export("QListWidget::staticMetaObject");
		detail::connect_function(this, signal, meta, [handler = std::move(handler)](void** args) {
			// args[0] is the return slot; the row arrives in args[1].
			if (handler && args && args[1])
				handler(*static_cast<const int*>(args[1]));
		});
	}
}
