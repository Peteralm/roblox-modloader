#pragma once

#include "RobloxModLoader/roblox/reflection/hints.hpp"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string_view>
#include <vector>

namespace RBX::Reflection
{
	class ClassDescriptor;
	class PropertyDescriptor;

	namespace Metadata
	{
		class Reflection;
	}
}

namespace rml::reflection
{
	struct PropertyMetadata
	{
		const RBX::Reflection::PropertyDescriptor* descriptor;
		PropertyHints hints;
	};

	struct ClassMetadata
	{
		const RBX::Reflection::ClassDescriptor* descriptor;
		bool owned_class{};
		ClassHints hints;
		std::vector<PropertyMetadata> properties;
	};

	class MetadataRegistry
	{
	public:
		enum class State : std::uint8_t
		{
			Pending,
			Applied,
			Disabled
		};

		static MetadataRegistry& instance();

		void add(ClassMetadata metadata);
		void on_tree_loaded(void* candidate);
		[[nodiscard]] State state() const;

	private:
		void try_apply(RBX::Reflection::Metadata::Reflection* root, bool required);
		void apply(RBX::Reflection::Metadata::Reflection& root, const ClassMetadata& metadata);
		void schedule_fallback();
		void disable(std::string_view reason);

		mutable std::recursive_mutex m_mutex;
		std::atomic<State> m_state{State::Pending};
		std::vector<ClassMetadata> m_pending;
		std::vector<ClassMetadata> m_applied;
		std::once_flag m_fallback_once;
	};
}
