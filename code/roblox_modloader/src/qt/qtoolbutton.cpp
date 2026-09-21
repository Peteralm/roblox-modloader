#include "RobloxModLoader/qt/qtoolbutton.hpp"

#include "RobloxModLoader/qt/qt_module.hpp"

namespace rml::qt
{
	QToolButton* QToolButton::create(QWidget* parent)
	{
		static const auto construct = detail::widgets<void* (*)(void*, void*)>(
		    "QToolButton::QToolButton(QWidget*)");
		return detail::heap_construct<QToolButton>(detail::WIDGET_INSTANCE_SIZE, construct, parent);
	}

	QtOwned<QToolButton> QToolButton::create_owned()
	{
		return QtOwned<QToolButton>(create(nullptr));
	}

	void QToolButton::destroy(QToolButton* button)
	{
		static const auto dtor = detail::widgets<void (*)(void*)>("QToolButton::~QToolButton()");
		detail::heap_destroy(dtor, button);
	}

	void QToolButton::set_auto_raise(const bool enabled)
	{
		static const auto fn = detail::widgets<void (*)(void*, bool)>("QToolButton::setAutoRaise(bool)");
		if (fn)
			fn(this, enabled);
	}
}
