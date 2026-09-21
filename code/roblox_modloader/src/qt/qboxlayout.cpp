#include "RobloxModLoader/qt/qboxlayout.hpp"

#include "RobloxModLoader/qt/qt_module.hpp"
#include "RobloxModLoader/qt/qwidget.hpp"

namespace rml::qt
{
	void QBoxLayout::add_widget(QWidget* widget, const int stretch)
	{
		static const auto fn = detail::widgets<void (*)(void*, void*, int, int)>(
		    "QBoxLayout::addWidget(QWidget*, int, QFlags<Qt::AlignmentFlag>)");
		if (fn && widget)
			fn(this, widget, stretch, 0);
	}

	void QBoxLayout::insert_widget(const int index, QWidget* widget, const int stretch)
	{
		static const auto fn = detail::widgets<void (*)(void*, int, void*, int, int)>(
		    "QBoxLayout::insertWidget(int, QWidget*, int, QFlags<Qt::AlignmentFlag>)");
		if (fn && widget)
			fn(this, index, widget, stretch, 0);
	}

	int QBoxLayout::count() const
	{
		static const auto fn = detail::widgets<int (*)(const void*)>("QLayout::count() const");
		return fn ? fn(this) : 0;
	}

	void QBoxLayout::set_contents_margins(const int left, const int top, const int right, const int bottom)
	{
		static const auto fn = detail::widgets<void (*)(void*, int, int, int, int)>(
		    "QLayout::setContentsMargins(int, int, int, int)");
		if (fn)
			fn(this, left, top, right, bottom);
	}

	void QBoxLayout::set_spacing(const int spacing)
	{
		static const auto fn = detail::widgets<void (*)(void*, int)>("QLayout::setSpacing(int)");
		if (fn)
			fn(this, spacing);
	}

	QVBoxLayout* QVBoxLayout::create(QWidget* parent)
	{
		static const auto construct = detail::widgets<void* (*)(void*, void*)>(
		    "QVBoxLayout::QVBoxLayout(QWidget*)");
		return detail::heap_construct<QVBoxLayout>(detail::WIDGET_INSTANCE_SIZE, construct, parent);
	}

	QHBoxLayout* QHBoxLayout::create(QWidget* parent)
	{
		static const auto construct = detail::widgets<void* (*)(void*, void*)>(
		    "QHBoxLayout::QHBoxLayout(QWidget*)");
		return detail::heap_construct<QHBoxLayout>(detail::WIDGET_INSTANCE_SIZE, construct, parent);
	}
}
