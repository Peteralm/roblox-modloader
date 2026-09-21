#pragma once

#include "RobloxModLoader/rml_export.hpp"
#include "RobloxModLoader/util/layout_assert.hpp"

namespace rml::qt
{
	class QMetaObject;

	class RML_EXPORT QObject
	{
	public:
		virtual const QMetaObject* metaObject() const = 0;
		virtual void* qt_metacast(const char* class_name) = 0;
		virtual int qt_metacall(int call, int id, void** args) = 0;
		virtual ~QObject() = default;
		virtual bool event(void* e) = 0;
		virtual bool eventFilter(void* watched, void* e) = 0;
		virtual void timerEvent(void* e) = 0;
		virtual void childEvent(void* e) = 0;
		virtual void customEvent(void* e) = 0;
		virtual void connectNotify(const void* signal) = 0;
		virtual void disconnectNotify(const void* signal) = 0;

		[[nodiscard]] const char* class_name() const;
		[[nodiscard]] bool inherits(const char* class_name) const;

		/// The thread this object belongs to; Qt only allows timers and widgets to
		/// be driven from it.
		[[nodiscard]] void* owner_thread() const;
		/// Hands the object over to another thread. The object must have no parent
		/// and must not be in use by its current thread.
		void move_to_thread(void* thread);
		/// Queues a no-argument slot call on the object's own thread and reports
		/// whether Qt accepted it. This is the only safe way to start a timer that
		/// was built on another thread.
		bool invoke_queued(const char* member);

		[[nodiscard]] void* handle()
		{
			return this;
		}
		[[nodiscard]] const void* handle() const
		{
			return this;
		}

	protected:
		void* d_ptr;

	private:
		RML_LAYOUT_GUARD_BEGIN()
			RML_ASSERT_LAYOUT_OFFSET(QObject, d_ptr, sizeof(void*));
			RML_ASSERT_LAYOUT_SIZE(QObject, sizeof(void*) * 2);
		RML_LAYOUT_GUARD_END()
	};
}
