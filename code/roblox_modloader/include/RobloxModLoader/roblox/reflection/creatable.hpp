#pragma once

#include <cstdint>
#include <memory>

namespace RBX
{
	class EngineContext;

	enum class CreatorRole : std::int32_t
	{
		Replication = 0,
		Serialization = 1,
		Scripting = 2,
		Engine = 3
	};

	class ICreator
	{
	public:
		virtual std::shared_ptr<void> create(EngineContext* context, CreatorRole role) const = 0;
		virtual bool is_serializable() const = 0;
		virtual bool is_script_creatable() const = 0;
	};
}
