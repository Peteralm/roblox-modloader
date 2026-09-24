#include "RobloxModLoader/roblox/graphics/render_camera.hpp"

#include "RobloxModLoader/roblox/util/Extents.h"

#include <cmath>
#include <cstring>

namespace RBX
{
	Frustum_SIMD::Frustum_SIMD(const Vector4* source_planes, const std::size_t count, const Vector4* corners)
	{
		std::memset(this, 0, sizeof(*this));

		for (std::size_t i = 0; i < count && i < 6; ++i)
			planes[i] = source_planes[i];

		for (int group = 0; group < 2; ++group)
			for (int lane = 0; lane < 4; ++lane)
			{
				const Vector4& plane = planes[group * 4 + lane];
				if (group == 1 && lane >= 2)
					break;

				planes_soa[group][0][lane] = plane.x;
				planes_soa[group][1][lane] = plane.y;
				planes_soa[group][2][lane] = plane.z;
				planes_soa[group][3][lane] = plane.w;
			}

		if (!corners)
			return;

		for (int group = 0; group < 2; ++group)
			for (int lane = 0; lane < 4; ++lane)
			{
				const Vector4& corner = corners[group * 4 + lane];
				corners_soa[group][0][lane] = corner.x;
				corners_soa[group][1][lane] = corner.y;
				corners_soa[group][2][lane] = corner.z;
			}
	}
}

namespace RBX::Graphics
{
	static Matrix4 pre_rotation(const PreRotate rotate)
	{
		Matrix4 result = Matrix4::identity();
		switch (rotate)
		{
		case PreRotate::Rotate90:
			result[0][0] = 0;
			result[0][1] = 1;
			result[1][0] = -1;
			result[1][1] = 0;
			break;
		case PreRotate::Rotate180:
			result[0][0] = -1;
			result[1][1] = -1;
			break;
		case PreRotate::Rotate270:
			result[0][0] = 0;
			result[0][1] = -1;
			result[1][0] = 1;
			result[1][1] = 0;
			break;
		default: break;
		}
		return result;
	}

	static Matrix4 projection_ortho(const float width, const float height, const float znear, const float zfar)
	{
		const float q = 1 / (zfar - znear);
		const float qn = -znear * q;

		return Matrix4(2 / width, 0, 0, -1, 0, 2 / height, 0, -1, 0, 0, q, qn, 0, 0, 0, 1);
	}

	static Matrix4 projection_perspective(const float fov_y, const float aspect, const float znear, const float zfar)
	{
		const float h = 1 / std::tan(fov_y / 2);
		const float w = h / aspect;

		const float q = -zfar / (zfar - znear);
		const float qn = znear * q;

		return Matrix4(w, 0, 0, 0, 0, h, 0, 0, 0, 0, q, qn, 0, 0, -1, 0);
	}

	static Matrix4 projection_perspective(const float fov_up_tan, const float fov_down_tan, const float fov_left_tan, const float fov_right_tan, const float znear, const float zfar)
	{
		const float sx = 2.f / (fov_left_tan + fov_right_tan);
		const float ox = (fov_right_tan - fov_left_tan) / (fov_left_tan + fov_right_tan);
		const float sy = 2.f / (fov_up_tan + fov_down_tan);
		const float oy = (fov_up_tan - fov_down_tan) / (fov_up_tan + fov_down_tan);

		const float q = -zfar / (zfar - znear);
		const float qn = znear * q;

		return Matrix4(sx, 0, ox, 0, 0, sy, oy, 0, 0, 0, q, qn, 0, 0, -1, 0);
	}

	static Vector4 normalize3(const Vector4& v)
	{
		return v / v.xyz().length();
	}

	RenderCamera::RenderCamera() :
	    view(Matrix4::zero()),
	    projection(Matrix4::zero()),
	    view_projection(Matrix4::zero()),
	    position(Vector3::zero()),
	    direction(Vector3::zero()),
	    up(Vector3::zero()),
	    projection_scale_y(0),
	    pre_rotate(PreRotate::None),
	    stereo_projection(Matrix4::zero()),
	    projection_no_pre_rotate(Matrix4::zero()),
	    stereo_separation(0),
	    stereo(false)
	{
		std::memset(&frustum, 0, sizeof(frustum));
		for (auto& corner : frustum_corners)
			corner = Vector4::zero();
	}

	void RenderCamera::set_view_cframe(const CoordinateFrame& cframe)
	{
		position = cframe.translation;
		direction = -cframe.rotation.column(2);
		up = cframe.rotation.column(1);
		view = cframe.inverse().toMatrix4();

		update_view_projection();
	}

	void RenderCamera::set_view_matrix(const Matrix4& value)
	{
		const Matrix4 inverse = value.inverse();
		position = inverse.column(3).xyz();
		direction = -inverse.column(2).xyz();
		up = inverse.column(1).xyz();
		view = value;

		update_view_projection();
	}

	void RenderCamera::translate_view_forward(const float distance)
	{
		const Vector3 offset = direction * distance;
		position += offset;

		for (int i = 0; i < 4; ++i)
			view[i][3] -= view.row(i).xyz().dot(offset);

		update_view_projection();
	}

	void RenderCamera::set_projection_perspective(const float fov_y, const float aspect, const float znear, const float zfar, const PreRotate rotate)
	{
		projection = pre_rotation(rotate) * projection_perspective(fov_y, aspect, znear, zfar);
		pre_rotate = rotate;
		update_view_projection();
		projection_scale_y = 1 / std::tan(fov_y / 2);
	}

	void RenderCamera::set_projection_perspective(const float fov_up_tan, const float fov_down_tan, const float fov_left_tan, const float fov_right_tan, const float znear, const float zfar, const PreRotate rotate)
	{
		const Matrix4 value = projection_perspective(fov_up_tan, fov_down_tan, fov_left_tan, fov_right_tan, znear, zfar);
		projection = pre_rotation(rotate) * value;
		pre_rotate = rotate;
		update_view_projection();
		projection_scale_y = value[1][1];
	}

	void RenderCamera::set_projection_ortho(const float width, const float height, const float znear, const float zfar, const PreRotate rotate)
	{
		const Matrix4 value = projection_ortho(width, height, znear, zfar);
		projection = pre_rotation(rotate) * value;
		pre_rotate = rotate;
		update_view_projection();
		projection_scale_y = value[1][1];
	}

	void RenderCamera::set_projection_matrix(const Matrix4& value, const PreRotate rotate)
	{
		projection = pre_rotation(rotate) * value;
		update_view_projection();
		projection_scale_y = value[1][1];
	}

	void RenderCamera::set_view_projection_matrix(const Matrix4& view_matrix, const Matrix4& projection_matrix)
	{
		view = view_matrix;
		projection = projection_matrix;
		update_view_projection();
	}

	void RenderCamera::scale_projection(const float x, const float y)
	{
		for (int c = 0; c < 4; ++c)
		{
			projection[0][c] *= x;
			projection[1][c] *= y;
		}
		update_view_projection();
	}

	void RenderCamera::change_projection_perspective_z(const float znear, const float zfar)
	{
		const float q = -zfar / (zfar - znear);
		const float qn = znear * q;

		projection[2][2] = q;
		projection[2][3] = qn;
		projection[3][2] = -1;
		projection[3][3] = 0;

		update_view_projection();
	}

	void RenderCamera::set_stereo(const Matrix4& view_matrix, const Matrix4& cull_view_projection, const float separation, const float fov_tans[4], const float znear, const float zfar)
	{
		stereo = true;
		stereo_separation = separation;
		stereo_projection = projection_perspective(fov_tans[0], fov_tans[1], fov_tans[2], fov_tans[3], znear, zfar);
		set_projection_perspective(fov_tans[0], fov_tans[1], fov_tans[2], fov_tans[3], znear, zfar, pre_rotate);
		set_view_matrix(view_matrix);
		update_frustum(cull_view_projection);
	}

	Matrix4 RenderCamera::get_view_matrix_eye(const int eye) const
	{
		Matrix4 result = view;
		if (stereo)
			result[0][3] += eye ? -stereo_separation : stereo_separation;
		return result;
	}

	Matrix4 RenderCamera::get_projection_matrix_eye(const int eye) const
	{
		if (!stereo)
			return projection;

		Matrix4 result = stereo_projection;
		if (eye)
			result[0][2] = -result[0][2];
		return result;
	}

	Matrix4 RenderCamera::get_view_projection_matrix_eye(const int eye) const
	{
		if (!stereo)
			return view_projection;
		return get_projection_matrix_eye(eye) * get_view_matrix_eye(eye);
	}

	Vector3 RenderCamera::get_position_eye(const int eye) const
	{
		if (!stereo)
			return position;
		return (get_view_matrix_eye(eye).inverse() * Vector4(0, 0, 0, 1)).xyz();
	}

	bool RenderCamera::is_visible(const Extents& extents) const
	{
		const Vector4 center = Vector4(extents.center(), 1);
		const Vector3 extent = extents.size() * 0.5f;

		for (const auto& plane : frustum.planes)
		{
			const float d = plane.dot(center);
			const float r = G3D::abs(plane.x) * extent.x + G3D::abs(plane.y) * extent.y + G3D::abs(plane.z) * extent.z;
			if (d + r < 0)
				return false;
		}

		return true;
	}

	bool RenderCamera::is_visible(const Sphere& sphere) const
	{
		const Vector4 center = Vector4(sphere.center, 1);

		for (const auto& plane : frustum.planes)
			if (plane.dot(center) + sphere.radius < 0)
				return false;

		return true;
	}

	bool RenderCamera::is_visible(const Extents& extents, const CoordinateFrame& cframe) const
	{
		const Vector3 center = cframe.pointToWorldSpace(extents.center());
		const Vector3 half = extents.size() * 0.5f;
		const Matrix3& r = cframe.rotation;
		const Vector3 world_half(G3D::abs(r[0][0]) * half.x + G3D::abs(r[0][1]) * half.y + G3D::abs(r[0][2]) * half.z,
		    G3D::abs(r[1][0]) * half.x + G3D::abs(r[1][1]) * half.y + G3D::abs(r[1][2]) * half.z,
		    G3D::abs(r[2][0]) * half.x + G3D::abs(r[2][1]) * half.y + G3D::abs(r[2][2]) * half.z);

		return is_visible(Extents(center - world_half, center + world_half));
	}

	IntersectResult RenderCamera::intersects(const Extents& extents) const
	{
		const Vector4 center = Vector4(extents.center(), 1);
		const Vector3 extent = extents.size() * 0.5f;

		IntersectResult result = irFull;

		for (const auto& plane : frustum.planes)
		{
			const float d = plane.dot(center);
			const float r = G3D::abs(plane.x) * extent.x + G3D::abs(plane.y) * extent.y + G3D::abs(plane.z) * extent.z;
			if (d + r < 0)
				return irNone;
			if (d - r < 0)
				result = irPartial;
		}

		return result;
	}

	void RenderCamera::update_view_projection()
	{
		projection_no_pre_rotate = pre_rotation(pre_rotate).inverse() * projection;
		view_projection = projection * view;
		update_frustum(view_projection);
	}

	void RenderCamera::update_frustum(const Matrix4& cull_view_projection)
	{
		Vector4 planes[6];
		planes[Plane_Near] = normalize3(cull_view_projection.row(2));
		planes[Plane_Far] = normalize3(cull_view_projection.row(3) - cull_view_projection.row(2));
		planes[Plane_Left] = normalize3(cull_view_projection.row(3) + cull_view_projection.row(0));
		planes[Plane_Right] = normalize3(cull_view_projection.row(3) - cull_view_projection.row(0));
		planes[Plane_Bottom] = normalize3(cull_view_projection.row(3) + cull_view_projection.row(1));
		planes[Plane_Top] = normalize3(cull_view_projection.row(3) - cull_view_projection.row(1));

		const Matrix4 inverse = cull_view_projection.inverse();
		const Vector3 clip_low(-1, -1, 0);
		const Vector3 clip_high(1, 1, 1);

		for (int i = 0; i < 8; ++i)
		{
			const Vector3 clip((i / 4) ? clip_high.x : clip_low.x,
			    ((i / 2) % 2) ? clip_high.y : clip_low.y,
			    (i % 2) ? clip_high.z : clip_low.z);
			const Vector4 corner = inverse * Vector4(clip, 1);
			frustum_corners[i] = Vector4(corner.xyz() / corner.w, 1);
		}

		frustum = Frustum_SIMD(planes, 6, frustum_corners);
	}
}
