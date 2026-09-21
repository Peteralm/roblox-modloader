#include "RobloxModLoader/qt/qboxlayout.hpp"

#include "RobloxModLoader/qt/qt_module.hpp"
#include "RobloxModLoader/qt/qwidget.hpp"

namespace rml::qt
{
	QVBoxLayout* QVBoxLayout::create(QWidget* parent)
	{
		static const auto construct = detail::widgets<void* (*)(void*, void*)>(
		    "QVBoxLayout::QVBoxLayout(QWidget*)");
		return detail::heap_construct<QVBoxLayout>(detail::WIDGET_INSTANCE_SIZE, construct, parent);
	}

	void QVBoxLayout::add_widget(QWidget* widget, const int stretch)
	{
		static const auto fn = detail::widgets<void (*)(void*, void*, int, int)>(
		    "QBoxLayout::addWidget(QWidget*, int, QFlags<Qt::AlignmentFlag>)");
		if (fn && widget)
			fn(this, widget, stretch, 0);
	}

	void QVBoxLayout::set_contents_margins(const int left, const int top, const int right, const int bottom)
	{
		static const auto fn = detail::widgets<void (*)(void*, int, int, int, int)>(
		    "QLayout::setContentsMargins(int, int, int, int)");
		if (fn)
			fn(this, left, top, right, bottom);
	}

	void QVBoxLayout::set_spacing(const int spacing)
	{
		static const auto fn = detail::widgets<void (*)(void*, int)>("QLayout::setSpacing(int)");
		if (fn)
			fn(this, spacing);
	}
}
