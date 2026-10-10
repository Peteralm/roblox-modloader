#pragma once

#include "cloud_settings.hpp"

#include <cstdint>
#include <mutex>
#include <optional>
#include <vector>

namespace clouds
{
	class CloudRegistry
	{
	public:
		static CloudRegistry& instance();

		void add(const void* owner, const CloudSettings& settings);
		void remove(const void* owner);
		void update(const void* owner, const CloudSettings& settings);
		void set_placed(const void* owner, bool placed);
		[[nodiscard]] std::optional<CloudSettings> active() const;

	private:
		struct Entry
		{
			const void* owner;
			CloudSettings settings;
			bool placed;
			std::uint64_t order;
		};

		Entry* find(const void* owner);

		mutable std::mutex m_mutex;
		std::vector<Entry> m_entries;
		std::uint64_t m_next_order{1};
	};
}
