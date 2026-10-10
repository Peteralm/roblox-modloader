#pragma once

#include "RobloxModLoader/render/injection_point.hpp"

#include <cstdint>

namespace rml::render::detail
{
	enum class PassState : std::uint8_t
	{
		Between,
		InPass
	};

	enum class SceneTarget : std::uint8_t
	{
		None,
		EnginePass,
		ViewOutput
	};

	struct InjectionTraits
	{
		PassState state;
		SceneTarget scene_target;
	};

	[[nodiscard]] constexpr InjectionTraits traits(const InjectionPoint point)
	{
		switch (point.kind())
		{
		case InjectionKind::Frame:
			switch (point.frame_point())
			{
			case FramePoint::MainAfterOpaque:
			case FramePoint::UIBefore:
				return {PassState::InPass, SceneTarget::EnginePass};
			case FramePoint::FrameEnd:
				return {PassState::Between, SceneTarget::ViewOutput};
			default:
				return {PassState::Between, SceneTarget::None};
			}
		case InjectionKind::Queue:
			return {PassState::InPass, SceneTarget::EnginePass};
		case InjectionKind::Stage:
			switch (point.engine_stage())
			{
			case EngineStage::Clouds:
			case EngineStage::Sky:
			case EngineStage::UI:
				return {PassState::InPass, SceneTarget::EnginePass};
			default:
				return {PassState::Between, SceneTarget::None};
			}
		}
		return {PassState::Between, SceneTarget::None};
	}
}
