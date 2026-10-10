#pragma once

#include "RobloxModLoader/rml_export.hpp"

#include <array>
#include <compare>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>

namespace rml::render
{
	enum class FramePoint : std::uint8_t
	{
		FrameBegin,
		CloudsPrepare,
		MainAfterOpaque,
		PostFXBefore,
		PostFXAfter,
		UIBefore,
		FrameEnd,
		Count
	};

	enum class QueueGroup : std::uint8_t
	{
		Opaque,
		Terrain,
		Decals,
		OpaqueCasters,
		OpaqueAdorns,
		OpaqueWithAlpha,
		Water,
		GlassTint,
		Glass,
		Transparent,
		TransparentCasters,
		OnTopWithDepth,
		OnTopReadOnlyDepth,
		AlwaysOnTop,
		AlwaysOnTopRobloxGui,
		AlwaysOnTopAdorns,
		Screen,
		ScreenOnTopOfBlur,
		Count
	};

	enum class EngineStage : std::uint8_t
	{
		Clouds,
		Sky,
		HBAO,
		SSAO,
		DepthOfField,
		Glow,
		SunRays,
		Blur,
		StudioSelections,
		Highlights,
		UI,
		Count
	};

	enum class Side : std::uint8_t
	{
		Before,
		After,
		Instead
	};

	enum class InjectionKind : std::uint8_t
	{
		Frame,
		Queue,
		Stage
	};

	inline constexpr std::array<std::string_view, static_cast<std::size_t>(FramePoint::Count)> k_frame_point_names{
	    "FrameBegin", "CloudsPrepare", "MainAfterOpaque", "PostFXBefore", "PostFXAfter", "UIBefore", "FrameEnd"};

	inline constexpr std::array<std::string_view, static_cast<std::size_t>(QueueGroup::Count)> k_queue_group_names{
	    "Opaque", "Terrain", "Decals", "OpaqueCasters", "OpaqueAdorns", "OpaqueWithAlpha", "Water", "GlassTint", "Glass",
	    "Transparent", "TransparentCasters", "OnTopWithDepth", "OnTopReadOnlyDepth", "AlwaysOnTop", "AlwaysOnTopRobloxGui",
	    "AlwaysOnTopAdorns", "Screen", "ScreenOnTopOfBlur"};

	inline constexpr std::array<std::string_view, static_cast<std::size_t>(EngineStage::Count)> k_engine_stage_names{
	    "Clouds", "Sky", "HBAO", "SSAO", "DepthOfField", "Glow", "SunRays", "Blur", "StudioSelections", "Highlights", "UI"};

	inline constexpr std::array<std::string_view, 3> k_side_names{"Before", "After", "Instead"};

	class InjectionPoint
	{
	public:
		[[nodiscard]] static constexpr InjectionPoint at(const FramePoint point)
		{
			return {InjectionKind::Frame, static_cast<std::uint8_t>(point), Side::Before};
		}

		[[nodiscard]] static constexpr InjectionPoint queue(const QueueGroup group, const Side side)
		{
			return {InjectionKind::Queue, static_cast<std::uint8_t>(group), side};
		}

		[[nodiscard]] static constexpr InjectionPoint stage(const EngineStage stage, const Side side)
		{
			return {InjectionKind::Stage, static_cast<std::uint8_t>(stage), side};
		}

		[[nodiscard]] constexpr InjectionKind kind() const
		{
			return m_kind;
		}

		[[nodiscard]] constexpr Side side() const
		{
			return m_side;
		}

		[[nodiscard]] constexpr FramePoint frame_point() const
		{
			return static_cast<FramePoint>(m_id);
		}

		[[nodiscard]] constexpr QueueGroup queue_group() const
		{
			return static_cast<QueueGroup>(m_id);
		}

		[[nodiscard]] constexpr EngineStage engine_stage() const
		{
			return static_cast<EngineStage>(m_id);
		}

		[[nodiscard]] constexpr std::uint32_t key() const
		{
			return static_cast<std::uint32_t>(m_kind) << 16 | static_cast<std::uint32_t>(m_id) << 8 | static_cast<std::uint32_t>(m_side);
		}

		friend constexpr bool operator==(const InjectionPoint&, const InjectionPoint&) = default;

		friend constexpr std::strong_ordering operator<=>(const InjectionPoint& lhs, const InjectionPoint& rhs)
		{
			return lhs.key() <=> rhs.key();
		}

	private:
		constexpr InjectionPoint(const InjectionKind kind, const std::uint8_t id, const Side side) :
		    m_kind(kind),
		    m_id(id),
		    m_side(side)
		{
		}

		InjectionKind m_kind;
		std::uint8_t m_id;
		Side m_side;
	};

	[[nodiscard]] inline std::string to_string(const InjectionPoint point)
	{
		const auto side = k_side_names[static_cast<std::size_t>(point.side())];
		switch (point.kind())
		{
		case InjectionKind::Frame:
			return std::string(k_frame_point_names[static_cast<std::size_t>(point.frame_point())]);
		case InjectionKind::Queue:
			return std::format("Queue.{}.{}", k_queue_group_names[static_cast<std::size_t>(point.queue_group())], side);
		case InjectionKind::Stage:
			return std::format("Stage.{}.{}", k_engine_stage_names[static_cast<std::size_t>(point.engine_stage())], side);
		}
		return {};
	}

	enum class InjectionStatus : std::uint8_t
	{
		Available,
		Unresolved,
		Inactive
	};

	RML_EXPORT InjectionStatus status(InjectionPoint point);
}
