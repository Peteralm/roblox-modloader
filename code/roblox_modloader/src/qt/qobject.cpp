#include "RobloxModLoader/qt/qobject.hpp"

#include "RobloxModLoader/qt/qmetaobject.hpp"
#include "RobloxModLoader/qt/qt_module.hpp"

namespace rml::qt
{
	const char* QObject::class_name() const
	{
		const QMetaObject* meta = metaObject();
		const char* name = meta ? meta->className() : nullptr;
		return name ? name : "";
	}

	bool QObject::inherits(const char* class_name) const
	{
		if (!class_name)
			return false;

		static const auto fn = detail::core_optional<bool (*)(const QObject*, const char*)>("QObject::inherits(char const*) const");
		if (fn)
			return fn(this, class_name);

		return const_cast<QObject*>(this)->qt_metacast(class_name) != nullptr;
	}

	void* QObject::owner_thread() const
	{
		static const auto fn = detail::core_optional<void* (*)(const void*)>("QObject::thread() const");
		return fn ? fn(this) : nullptr;
	}

	void QObject::move_to_thread(void* thread)
	{
		static const auto fn = detail::core_optional<void (*)(void*, void*)>("QObject::moveToThread(QThread*)");
		if (fn && thread)
			fn(this, thread);
	}

	bool QObject::invoke_queued(const char* member)
	{
		// QGenericArgument is {const char* name; void* data;}; a default-built one
		// is two null words, and MSVC passes it by address because it is 16 bytes.
		struct GenericArgument
		{
			const char* name{};
			void* data{};
		};
		using InvokeMethod = bool (*)(void*, const char*, int, const GenericArgument*, const GenericArgument*, const GenericArgument*, const GenericArgument*, const GenericArgument*, const GenericArgument*, const GenericArgument*, const GenericArgument*, const GenericArgument*, const GenericArgument*);
		static const auto fn = detail::core_optional<InvokeMethod>("QMetaObject::invokeMethod(QObject*, char const*, Qt::ConnectionType, QGenericArgument, "
		                                                           "QGenericArgument, QGenericArgument, QGenericArgument, QGenericArgument, QGenericArgument, "
		                                                           "QGenericArgument, QGenericArgument, QGenericArgument, QGenericArgument)");
		if (!fn || !member)
			return false;
		static constexpr GenericArgument kEmpty{};
		// Qt::QueuedConnection == 2: the call is delivered by the receiver's own
		// event loop, which is exactly the thread hop this is for.
		return fn(this, member, 2, &kEmpty, &kEmpty, &kEmpty, &kEmpty, &kEmpty, &kEmpty, &kEmpty, &kEmpty, &kEmpty, &kEmpty);
	}
}
