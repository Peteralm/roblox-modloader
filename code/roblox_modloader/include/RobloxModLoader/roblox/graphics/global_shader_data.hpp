#pragma once

#include "RobloxModLoader/rml_export.hpp"
#include "RobloxModLoader/roblox/util/G3DCore.h"
#include "RobloxModLoader/util/layout_assert.hpp"

namespace RBX::Graphics
{
	class RenderCamera;

	struct GlobalShaderData
	{
		Matrix4 view_projection[2];
		Vector4 view_right;
		Vector4 view_up;
		Vector4 view_dir;
		Vector4 camera_position[2];

		Vector4 ambient_color;
		Vector4 outdoor_ambient_color;
		Vector4 lamp0_color;
		Vector4 lamp0_dir;
		Vector4 lamp1_color;

		Vector4 fog_params;
		Vector4 fog_color;

		float reserved_320[3];
		float depth_of_field_focus;
		Vector4 reserved_336[8];

		Vector4 outline_brightness_shadow_info;

		Vector4 ibl_diffuse;
		Vector4 ibl_specular;

		Vector4 reserved_512[21];

		float reserved_848[2];
		float opaque_with_alpha;
		float reserved_860;

		Matrix4 reserved_864;
		Vector4 reserved_928[3];

		RML_EXPORT void set_camera(const RenderCamera& camera);
	};

	RML_LAYOUT_DIAGNOSTIC_PUSH()
	RML_ASSERT_OFFSET(GlobalShaderData, view_right, 128);
	RML_ASSERT_OFFSET(GlobalShaderData, camera_position, 176);
	RML_ASSERT_OFFSET(GlobalShaderData, ambient_color, 208);
	RML_ASSERT_OFFSET(GlobalShaderData, lamp0_color, 240);
	RML_ASSERT_OFFSET(GlobalShaderData, fog_params, 288);
	RML_ASSERT_OFFSET(GlobalShaderData, fog_color, 304);
	RML_ASSERT_OFFSET(GlobalShaderData, depth_of_field_focus, 332);
	RML_ASSERT_OFFSET(GlobalShaderData, outline_brightness_shadow_info, 464);
	RML_ASSERT_OFFSET(GlobalShaderData, ibl_diffuse, 480);
	RML_ASSERT_OFFSET(GlobalShaderData, opaque_with_alpha, 856);
	RML_ASSERT_OFFSET(GlobalShaderData, reserved_864, 864);
	RML_ASSERT_SIZE(GlobalShaderData, 976);
	RML_LAYOUT_DIAGNOSTIC_POP()
}
