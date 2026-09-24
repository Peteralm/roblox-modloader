#include "RobloxModLoader/roblox/graphics/global_shader_data.hpp"

#include "RobloxModLoader/roblox/graphics/render_camera.hpp"

namespace RBX::Graphics
{
	void GlobalShaderData::set_camera(const RenderCamera& camera)
	{
		const Matrix3 axes = camera.view.upper3x3().transpose();
		view_right = Vector4(axes.column(0), 0);
		view_up = Vector4(axes.column(1), 0);
		view_dir = Vector4(axes.column(2), 0);

		const int eyes = camera.stereo ? 2 : 1;
		for (int eye = 0; eye < eyes; ++eye)
		{
			view_projection[eye] = camera.get_view_projection_matrix_eye(eye);
			camera_position[eye] = Vector4(camera.get_position_eye(eye), 1);
		}
	}
}
