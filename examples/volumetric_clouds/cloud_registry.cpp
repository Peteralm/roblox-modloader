#include "cloud_registry.hpp"

#include <algorithm>

namespace clouds
{
	CloudRegistry& CloudRegistry::instance()
	{
		static CloudRegistry registry;
		return registry;
	}

	CloudRegistry::Entry* CloudRegistry::find(const void* owner)
	{
		const auto it = std::ranges::find(m_entries, owner, &Entry::owner);
		return it == m_entries.end() ? nullptr : &*it;
	}

	void CloudRegistry::add(const void* owner, const CloudSettings& settings)
	{
		std::lock_guard lock(m_mutex);
		m_entries.push_back({owner, settings, false, 0});
	}

	void CloudRegistry::remove(const void* owner)
	{
		std::lock_guard lock(m_mutex);
		std::erase_if(m_entries, [owner](const Entry& entry) { return entry.owner == owner; });
	}

	void CloudRegistry::update(const void* owner, const CloudSettings& settings)
	{
		std::lock_guard lock(m_mutex);
		auto* entry = find(owner);
		if (!entry)
			return;
		if (settings.enabled && !entry->settings.enabled)
			entry->order = m_next_order++;
		entry->settings = settings;
	}

	void CloudRegistry::set_placed(const void* owner, const bool placed)
	{
		std::lock_guard lock(m_mutex);
		auto* entry = find(owner);
		if (!entry || entry->placed == placed)
			return;
		entry->placed = placed;
		if (placed)
			entry->order = m_next_order++;
	}

	std::optional<CloudSettings> CloudRegistry::active() const
	{
		std::lock_guard lock(m_mutex);
		const Entry* best = nullptr;
		for (const auto& entry : m_entries)
		{
			if (entry.placed && entry.settings.enabled && (!best || entry.order > best->order))
				best = &entry;
		}
		return best ? std::optional(best->settings) : std::nullopt;
	}
}
