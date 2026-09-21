#pragma once

#include <chrono>
#include <condition_variable>
#include <mutex>

namespace rml
{
	enum class InitGateState
	{
		Waiting,
		Reached,
		Released,
		Missed
	};

	class InitLatch
	{
	public:
		void mark_reached()
		{
			std::lock_guard lock(m_mutex);
			if (m_state == InitGateState::Waiting)
				m_state = InitGateState::Reached;
		}

		void release()
		{
			{
				std::lock_guard lock(m_mutex);
				if (m_state != InitGateState::Missed)
					m_state = InitGateState::Released;
				m_released = true;
			}
			m_cv.notify_all();
		}

		bool wait(std::chrono::milliseconds timeout)
		{
			std::unique_lock lock(m_mutex);
			return m_cv.wait_for(lock, timeout, [this] { return m_released; });
		}

		void mark_missed()
		{
			std::lock_guard lock(m_mutex);
			m_state = InitGateState::Missed;
		}

		InitGateState state() const
		{
			std::lock_guard lock(m_mutex);
			return m_state;
		}

	private:
		mutable std::mutex m_mutex;
		std::condition_variable m_cv;
		InitGateState m_state{InitGateState::Waiting};
		bool m_released{false};
	};
}
