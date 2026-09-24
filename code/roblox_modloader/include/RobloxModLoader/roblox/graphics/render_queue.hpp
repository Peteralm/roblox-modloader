#pragma once

#include "RobloxModLoader/roblox/graphics/buffer.hpp"
#include "RobloxModLoader/util/layout_assert.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace RBX::Graphics
{
	class DeviceContext;
	class Technique;
	class ViewContext;
	struct RenderableStats;
	struct RenderOperation;

	class Renderable
	{
	public:
		virtual ~Renderable() = default;
		virtual void upload_buffer_data(DeviceContext* context, const ViewContext& view, const RenderOperation* operations, std::size_t count, RenderableStats& stats) const
		{
		}
		virtual void render(DeviceContext* context, const ViewContext& view, const RenderOperation* operations, std::size_t count, RenderableStats& stats) const = 0;
		virtual bool get_instance_index_buffer(const ViewContext& view, const RenderOperation* operations, std::size_t count, std::vector<unsigned>& indices) const
		{
			return false;
		}
		virtual void set_instance_index_buffer_offset_count(unsigned offset, unsigned count) const
		{
		}
	};

	struct RenderOperation
	{
		const Renderable* renderable;
		const Technique* technique;
		const GeometryBatch* geometry;
		float distance_key;
		float radius;
		union
		{
			std::int32_t z_index;
			std::uint32_t distance_bucket;
		};
		std::uint8_t reserved_36;
		std::uint8_t reserved_37;
		std::uint8_t reserved_38;
		std::uint8_t reserved_39;

		static RenderOperation make(const Renderable* renderable, const Technique* technique, const GeometryBatch* geometry, const float distance_key = 0.0f, const float radius = 0.0f, const std::int32_t z_index = 0)
		{
			RenderOperation op;
			op.renderable = renderable;
			op.technique = technique;
			op.geometry = geometry;
			op.distance_key = distance_key;
			op.radius = radius;
			op.z_index = z_index;
			op.reserved_36 = 0;
			op.reserved_37 = 3;
			op.reserved_38 = 5;
			op.reserved_39 = 4;
			return op;
		}

		bool clip_at_distance(const float distance, const bool near_side) const
		{
			if (near_side)
				return distance > std::max(radius + radius, distance_key) + radius;
			return distance_key - radius > distance;
		}
	};

	RML_LAYOUT_DIAGNOSTIC_PUSH()
	RML_ASSERT_OFFSET(RenderOperation, geometry, 16);
	RML_ASSERT_OFFSET(RenderOperation, distance_key, 24);
	RML_ASSERT_OFFSET(RenderOperation, z_index, 32);
	RML_ASSERT_SIZE(RenderOperation, 40);
	RML_LAYOUT_DIAGNOSTIC_POP()

	class RenderQueueGroup
	{
	public:
		enum SortMode : int
		{
			Sort_None,
			Sort_Material,
			Sort_Distance,
			Sort_DistanceAndZIndex,
			Sort_ZIndexDistance,
			Sort_ReverseDistance,
			Sort_MaterialAndReverseDistance,
			Sort_DistanceBucketAndMaterial,
			Sort_MaterialAndIB,
			Sort_ZIndexMaterialAndIB
		};

		std::vector<RenderOperation> operations;

		void clear()
		{
			operations.clear();
		}

		void push(const RenderOperation& op)
		{
			operations.push_back(op);
		}

		const RenderOperation& operator[](const std::size_t index) const
		{
			return operations[index];
		}

		std::size_t size() const
		{
			return operations.size();
		}

		bool empty() const
		{
			return operations.empty();
		}

		const RenderOperation* begin() const
		{
			return operations.data();
		}

		const RenderOperation* end() const
		{
			return operations.data() + operations.size();
		}
	};

	RML_LAYOUT_DIAGNOSTIC_PUSH()
	RML_ASSERT_SIZE(RenderQueueGroup, 24);
	RML_LAYOUT_DIAGNOSTIC_POP()

	class RenderQueue
	{
	public:
		enum Pass : int
		{
			Pass_Default,
			Pass_GlassTint,
			Pass_Shadows,
			Pass_Depth,
			Pass_DepthVT,
			Pass_CastShadowsPancaked,
			Pass_CastShadows,
			Pass_Backfaces,
			Pass_EnvMap,
			Pass_HighlightPostProcess,
			Pass_HighlightInPlace,
			Pass_HighlightMobileDepthTestMarker,
			Pass_HighlightMobileIds,
			Pass_PerformanceOverlay,
			Pass_StudioSelection,
			Pass_StudioSelectionHover,
			Pass_StudioSelectionActive,
			Pass_StudioSelectionTeamCreate,
			Pass_StudioSelectionNegated,
			Pass_TerrainWithVT,
			Pass_TerrainVTFeedback,
			Pass_Count
		};

		enum Id : int
		{
			Id_Opaque,
			Id_Terrain,
			Id_Decals,
			Id_OpaqueCasters,
			Id_OpaqueAdorns,
			Id_OpaqueWithAlpha,
			Id_Water,
			Id_GlassTint,
			Id_Glass,
			Id_Transparent,
			Id_TransparentCasters,
			Id_OnTopWithDepth,
			Id_OnTopReadOnlyDepth,
			Id_AlwaysOnTop,
			Id_AlwaysOnTopRobloxGui,
			Id_AlwaysOnTopAdorns,
			Id_Screen,
			Id_ScreenOnTopOfBlur,
			Id_Count
		};

		enum Features : unsigned
		{
			Features_Glow = 1 << 0
		};

		static constexpr const char* pass_names[Pass_Count] = {
		    "Pass_Default",
		    "Pass_GlassTint",
		    "Pass_Shadows",
		    "Pass_Depth",
		    "Pass_DepthVT",
		    "Pass_CastShadowsPancaked",
		    "Pass_CastShadows",
		    "Pass_Backfaces",
		    "Pass_EnvMap",
		    "Pass_HighlightPostProcess",
		    "Pass_HighlightInPlace",
		    "Pass_HighlightMobileDepthTestMarker",
		    "Pass_HighlightMobileIds",
		    "Pass_PerformanceOverlay",
		    "Pass_StudioSelection",
		    "Pass_StudioSelectionHover",
		    "Pass_StudioSelectionActive",
		    "Pass_StudioSelectionTeamCreate",
		    "Pass_StudioSelectionNegated",
		    "Pass_TerrainWithVT",
		    "Pass_TerrainVTFeedback",
		};

		static constexpr const char* id_names[Id_Count] = {
		    "Id_Opaque",
		    "Id_Terrain",
		    "Id_Decals",
		    "Id_OpaqueCasters",
		    "Id_OpaqueAdorns",
		    "Id_OpaqueWithAlpha",
		    "Id_Water",
		    "Id_GlassTint",
		    "Id_Glass",
		    "Id_Transparent",
		    "Id_TransparentCasters",
		    "Id_OnTopWithDepth",
		    "Id_OnTopReadOnlyDepth",
		    "Id_AlwaysOnTop",
		    "Id_AlwaysOnTopRobloxGui",
		    "Id_AlwaysOnTopAdorns",
		    "Id_Screen",
		    "Id_ScreenOnTopOfBlur",
		};

		std::uint64_t reserved_0[5];
		std::uint32_t reserved_40;
		std::uint32_t reserved_44[8];
		std::uint32_t reserved_76;
		std::uint8_t reserved_80[16];
		bool reserved_96;
		std::uint8_t reserved_97[7];
		void* reserved_104;
		RenderQueueGroup groups[Id_Count];
		unsigned features;
		std::uint32_t reserved_548;

		RenderQueueGroup& get_group(const Id id)
		{
			return groups[id];
		}

		const RenderQueueGroup& get_group(const Id id) const
		{
			return groups[id];
		}

		unsigned get_features() const
		{
			return features;
		}

		void set_feature(const unsigned feature)
		{
			features |= feature;
		}

		void clear()
		{
			for (auto& group : groups)
				group.clear();
			reserved_104 = nullptr;
			features = 0;
			reserved_548 = 0xFFFFFFFFu;
			reserved_40 = 0;
			reserved_76 = 0;
		}

	private:
		RenderQueue() = delete;
	};

	RML_LAYOUT_DIAGNOSTIC_PUSH()
	RML_ASSERT_OFFSET(RenderQueue, reserved_40, 40);
	RML_ASSERT_OFFSET(RenderQueue, reserved_76, 76);
	RML_ASSERT_OFFSET(RenderQueue, reserved_96, 96);
	RML_ASSERT_OFFSET(RenderQueue, groups, 112);
	RML_ASSERT_OFFSET(RenderQueue, features, 544);
	RML_ASSERT_SIZE(RenderQueue, 552);
	RML_LAYOUT_DIAGNOSTIC_POP()
}
