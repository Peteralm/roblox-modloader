#pragma once

#include "RobloxModLoader/render/injection_point.hpp"
#include "RobloxModLoader/rml_export.hpp"
#include "RobloxModLoader/util/flags.hpp"

#include <cstdint>

namespace rml::render
{
	using EngineStages = Flags<EngineStage>;

	class RML_EXPORT SkipToken
	{
	public:
		SkipToken() = default;
		explicit SkipToken(std::uint64_t id) noexcept;
		~SkipToken();

		SkipToken(const SkipToken&) = delete;
		SkipToken& operator=(const SkipToken&) = delete;
		SkipToken(SkipToken&& other) noexcept;
		SkipToken& operator=(SkipToken&& other) noexcept;

		[[nodiscard]] bool active() const noexcept
		{
			return m_id != 0;
		}

		void reset() noexcept;

	private:
		std::uint64_t m_id{};
	};

	[[nodiscard]] RML_EXPORT SkipToken skip(EngineStage stage);
	[[nodiscard]] RML_EXPORT SkipToken skip(QueueGroup group);
	[[nodiscard]] RML_EXPORT bool is_skippable(EngineStage stage);
	[[nodiscard]] RML_EXPORT bool is_skippable(QueueGroup group);
}
