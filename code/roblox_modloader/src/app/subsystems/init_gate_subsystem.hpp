#pragma once

#include "../init_gate.hpp"
#include "../isubsystem.hpp"

namespace rml
{
	class InitGateSubsystem final : public ISubsystem
	{
	public:
		std::expected<void, SubsystemError> initialize() override
		{
			m_instance = std::make_unique<InitGate>();
			if (const auto result = m_instance->install(); !result)
				RML_WARN_AT("InitGate", "Init gate not installed: {}", result.error());
			return {};
		}

		void shutdown() override
		{
			m_instance.reset();
		}

		[[nodiscard]] std::string_view name() const noexcept override
		{
			return "InitGate";
		}

	private:
		std::unique_ptr<InitGate> m_instance;
	};
}
