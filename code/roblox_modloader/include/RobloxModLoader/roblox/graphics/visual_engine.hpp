#pragma once

#include "RobloxModLoader/util/layout_assert.hpp"

#include <cstddef>
#include <memory>

namespace RBX::Graphics
{
	class Device;
	class IShaderManager;
	class SceneManager;

	class VisualEngine
	{
	public:
		std::byte reserved_0[0x108];
		Device* device;
		std::byte reserved_110[0xAB8];
		union {
			std::unique_ptr<IShaderManager> shader_manager;
		};
		std::byte reserved_bd0[0x10];
		IShaderManager* external_shader_manager;
		std::byte reserved_be8[0x8];
		union {
			std::unique_ptr<SceneManager> scene_manager;
		};

		IShaderManager* get_shader_manager() const
		{
			return external_shader_manager ? external_shader_manager : shader_manager.get();
		}

		SceneManager* get_scene_manager() const
		{
			return scene_manager.get();
		}

	private:
		VisualEngine() = delete;
		~VisualEngine() = delete;
	};

	RML_LAYOUT_DIAGNOSTIC_PUSH()
	RML_ASSERT_OFFSET(VisualEngine, device, 0x108);
	RML_ASSERT_OFFSET(VisualEngine, shader_manager, 0xBC8);
	RML_ASSERT_OFFSET(VisualEngine, external_shader_manager, 0xBE0);
	RML_ASSERT_OFFSET(VisualEngine, scene_manager, 0xBF0);
	RML_LAYOUT_DIAGNOSTIC_POP()
}
