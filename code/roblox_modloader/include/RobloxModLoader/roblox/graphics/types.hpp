#pragma once

#include "RobloxModLoader/util/layout_assert.hpp"

#include <cstdint>

namespace RBX
{
	struct ClientSessionMapFieldContainer;

	namespace Profiler
	{
		struct ActiveRegion;
	}
}

namespace RBX::Graphics
{
	struct TextureRegion
	{
		unsigned x;
		unsigned y;
		unsigned z;
		unsigned width;
		unsigned height;
		unsigned depth;
	};

	struct SamplerState
	{
		enum Filter : std::uint8_t
		{
			Filter_Point,
			Filter_Linear,
			Filter_Anisotropic
		};

		enum Address : std::uint8_t
		{
			Address_Wrap,
			Address_Clamp,
			Address_Border
		};

		std::uint8_t filter;
		std::uint8_t address;
		std::uint8_t comparison;
		std::uint8_t border_color;
		std::uint32_t anisotropy;
		float lod_min;
		float lod_max;
	};

	struct RasterizerState
	{
		enum CullMode : std::uint8_t
		{
			Cull_None,
			Cull_Back,
			Cull_Front
		};

		enum FillMode : std::uint8_t
		{
			Fill_Solid,
			Fill_Wireframe
		};

		std::uint8_t cull_mode;
		std::uint8_t fill_mode;
		std::uint8_t reserved_2;
		std::uint8_t depth_clip_mode;
		std::int32_t depth_bias;
	};

	struct BlendState
	{
		enum Factor : std::uint8_t
		{
			Factor_One,
			Factor_Zero,
			Factor_DstColor,
			Factor_SrcAlpha,
			Factor_InvSrcAlpha,
			Factor_DstAlpha,
			Factor_InvDstAlpha
		};

		enum ColorMask : std::uint8_t
		{
			Color_None = 0,
			Color_R = 1 << 0,
			Color_G = 1 << 1,
			Color_B = 1 << 2,
			Color_A = 1 << 3,
			Color_All = Color_R | Color_G | Color_B | Color_A
		};

		std::uint8_t color_mask;
		std::uint8_t reserved_1[3];
		std::uint8_t src_rgb;
		std::uint8_t dst_rgb;
		std::uint8_t src_alpha;
		std::uint8_t dst_alpha;
		std::uint8_t alpha_to_coverage;
		std::uint8_t reserved_9[3];
	};

	struct DepthState
	{
		enum Function : std::uint8_t
		{
			Function_Always,
			Function_Less,
			Function_LessEqual,
			Function_Greater,
			Function_GreaterEqual,
			Function_Equal,
			Function_NotEqual
		};

		std::uint8_t function;
		std::uint8_t write;
		std::uint8_t stencil_mode;
		std::uint8_t reserved_3;
	};

	RML_LAYOUT_DIAGNOSTIC_PUSH()
	RML_ASSERT_SIZE(TextureRegion, 24);
	RML_ASSERT_SIZE(SamplerState, 16);
	RML_ASSERT_SIZE(RasterizerState, 8);
	RML_ASSERT_SIZE(BlendState, 12);
	RML_ASSERT_SIZE(DepthState, 4);
	RML_LAYOUT_DIAGNOSTIC_POP()

	struct PassClear
	{
		enum Mask : std::uint32_t
		{
			Color0 = 1 << 0,
			Color1 = 1 << 1,
			Color2 = 1 << 2,
			Color3 = 1 << 3,
			Depth = 1 << 4,
			Stencil = 1 << 5,
			All = 0x3F
		};

		std::uint32_t mask;
		std::uint32_t reserved_4[2];
		float color[4][4];
		float depth;
		std::uint32_t stencil;
	};

	RML_LAYOUT_DIAGNOSTIC_PUSH()
	RML_ASSERT_SIZE(PassClear, 84);
	RML_LAYOUT_DIAGNOSTIC_POP()

	struct PassResolve;
	struct ConstantBuffer;
	struct DeviceCaps;
	struct DeviceStats;
	struct VideoMemoryInfo;
	struct FrameTimingDetails;
	struct DeviceRecoveryTelemetryParams;
	class DeviceVR;
	class TextureDownloadBuffer;
}
