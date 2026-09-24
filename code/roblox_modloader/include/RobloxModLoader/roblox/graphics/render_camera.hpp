#pragma once

#include "RobloxModLoader/rml_export.hpp"
#include "RobloxModLoader/roblox/util/G3DCore.h"
#include "RobloxModLoader/util/layout_assert.hpp"

#include <cstddef>

namespace RBX
{
	class Extents;

	struct alignas(16) Frustum_SIMD
	{
		Vector4 planes[6];
		Vector4 planes_soa[2][4];
		Vector4 corners_soa[2][3];

		Frustum_SIMD() = default;
		RML_EXPORT Frustum_SIMD(const Vector4* planes, std::size_t count, const Vector4* corners);
	};

	RML_LAYOUT_DIAGNOSTIC_PUSH()
	RML_ASSERT_OFFSET(Frustum_SIMD, planes_soa, 96);
	RML_ASSERT_OFFSET(Frustum_SIMD, corners_soa, 224);
	RML_ASSERT_SIZE(Frustum_SIMD, 320);
	RML_LAYOUT_DIAGNOSTIC_POP()
}

namespace RBX::Graphics
{
	enum class PreRotate : int
	{
		None,
		Rotate90,
		Rotate180,
		Rotate270
	};

	class alignas(16) RenderCamera
	{
	public:
		enum FrustumPlane
		{
			Plane_Near,
			Plane_Far,
			Plane_Left,
			Plane_Right,
			Plane_Bottom,
			Plane_Top
		};

		RML_EXPORT RenderCamera();

		RML_EXPORT void set_view_cframe(const CoordinateFrame& cframe);
		RML_EXPORT void set_view_matrix(const Matrix4& value);
		RML_EXPORT void translate_view_forward(float distance);

		RML_EXPORT void set_projection_perspective(float fov_y, float aspect, float znear, float zfar, PreRotate rotate = PreRotate::None);
		RML_EXPORT void set_projection_perspective(float fov_up_tan, float fov_down_tan, float fov_left_tan, float fov_right_tan, float znear, float zfar, PreRotate rotate = PreRotate::None);
		RML_EXPORT void set_projection_ortho(float width, float height, float znear, float zfar, PreRotate rotate = PreRotate::None);
		RML_EXPORT void set_projection_matrix(const Matrix4& value, PreRotate rotate = PreRotate::None);
		RML_EXPORT void set_view_projection_matrix(const Matrix4& view, const Matrix4& projection);
		RML_EXPORT void scale_projection(float x, float y);
		RML_EXPORT void change_projection_perspective_z(float znear, float zfar);
		RML_EXPORT void set_stereo(const Matrix4& view, const Matrix4& cull_view_projection, float separation, const float fov_tans[4], float znear, float zfar);

		const Matrix4& get_view_matrix() const
		{
			return view;
		}

		const Matrix4& get_projection_matrix() const
		{
			return projection;
		}

		const Matrix4& get_view_projection_matrix() const
		{
			return view_projection;
		}

		const Vector3& get_position() const
		{
			return position;
		}

		const Vector3& get_direction() const
		{
			return direction;
		}

		const Vector3& get_up() const
		{
			return up;
		}

		const Vector4& get_frustum_plane(FrustumPlane plane) const
		{
			return frustum.planes[plane];
		}

		RML_EXPORT Matrix4 get_view_matrix_eye(int eye) const;
		RML_EXPORT Matrix4 get_projection_matrix_eye(int eye) const;
		RML_EXPORT Matrix4 get_view_projection_matrix_eye(int eye) const;
		RML_EXPORT Vector3 get_position_eye(int eye) const;

		RML_EXPORT bool is_visible(const Extents& extents) const;
		RML_EXPORT bool is_visible(const Sphere& sphere) const;
		RML_EXPORT bool is_visible(const Extents& extents, const CoordinateFrame& cframe) const;
		RML_EXPORT IntersectResult intersects(const Extents& extents) const;

		Matrix4 view;
		Matrix4 projection;
		Matrix4 view_projection;
		Vector3 position;
		Vector3 direction;
		Vector3 up;
		float projection_scale_y;
		PreRotate pre_rotate;
		Frustum_SIMD frustum;
		Matrix4 stereo_projection;
		Matrix4 projection_no_pre_rotate;
		Vector4 frustum_corners[8];
		float stereo_separation;
		bool stereo;

	private:
		RML_EXPORT void update_view_projection();
		RML_EXPORT void update_frustum(const Matrix4& cull_view_projection);
	};

	RML_LAYOUT_DIAGNOSTIC_PUSH()
	RML_ASSERT_OFFSET(RenderCamera, projection, 64);
	RML_ASSERT_OFFSET(RenderCamera, view_projection, 128);
	RML_ASSERT_OFFSET(RenderCamera, position, 192);
	RML_ASSERT_OFFSET(RenderCamera, direction, 204);
	RML_ASSERT_OFFSET(RenderCamera, up, 216);
	RML_ASSERT_OFFSET(RenderCamera, projection_scale_y, 228);
	RML_ASSERT_OFFSET(RenderCamera, pre_rotate, 232);
	RML_ASSERT_OFFSET(RenderCamera, frustum, 240);
	RML_ASSERT_OFFSET(RenderCamera, stereo_projection, 560);
	RML_ASSERT_OFFSET(RenderCamera, projection_no_pre_rotate, 624);
	RML_ASSERT_OFFSET(RenderCamera, frustum_corners, 688);
	RML_ASSERT_OFFSET(RenderCamera, stereo_separation, 816);
	RML_ASSERT_OFFSET(RenderCamera, stereo, 820);
	RML_ASSERT_SIZE(RenderCamera, 832);
	RML_LAYOUT_DIAGNOSTIC_POP()
}
