#include "RobloxModLoader/qt/qaction.hpp"

#include "RobloxModLoader/qt/qicon.hpp"
#include "RobloxModLoader/qt/qt_module.hpp"
#include "RobloxModLoader/memory/foreign_call.hpp"
#include "RobloxModLoader/qt/qstring.hpp"

#include <cstring>

namespace rml::qt
{
	void QAction::setCheckable(const bool checkable)
	{
		static const auto fn = detail::widgets<void (*)(void*, bool)>("QAction::setCheckable(bool)");
		if (fn)
			fn(this, checkable);
	}

	void QAction::setChecked(const bool checked)
	{
		static const auto fn = detail::widgets<void (*)(void*, bool)>("QAction::setChecked(bool)");
		if (fn)
			fn(this, checked);
	}

	bool QAction::isChecked() const
	{
		static const auto fn = detail::widgets<bool (*)(const void*)>("QAction::isChecked() const");
		return fn && fn(this);
	}

	std::string QAction::text() const
	{
		static void* const get = detail::widgets_export("QAction::text() const");
		if (!get)
			return {};

		// The getter returns QString by value, so the caller owns the storage.
		void* storage = nullptr;
		memory::call_returning_member(get, storage, static_cast<const void*>(this));
		QString value;
		std::memcpy(value.storage(), &storage, sizeof(storage));
		return value.to_utf8();
	}

	QMenu* QAction::menu() const
	{
		static const auto fn = detail::widgets<void* (*)(const void*)>("QAction::menu() const");
		return fn ? static_cast<QMenu*>(fn(this)) : nullptr;
	}

	void QAction::trigger()
	{
		static const auto fn = detail::widgets<void (*)(void*)>("QAction::trigger()");
		if (fn)
			fn(this);
	}

	void QAction::setIcon(const QIcon& icon)
	{
		static const auto fn = detail::widgets<void (*)(void*, const void*)>("QAction::setIcon(QIcon const&)");
		if (fn)
			fn(this, icon.data());
	}

	void QAction::setMenuRole(const MenuRole role)
	{
		static const auto fn = detail::widgets<void (*)(void*, int)>("QAction::setMenuRole(QAction::MenuRole)");
		if (fn)
			fn(this, static_cast<int>(role));
	}

	void* QAction::activate_address()
	{
		return detail::widgets_export("QAction::activate(QAction::ActionEvent)");
	}
}
