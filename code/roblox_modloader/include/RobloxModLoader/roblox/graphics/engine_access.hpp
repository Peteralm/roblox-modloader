#pragma once

#include "RobloxModLoader/rml_export.hpp"

#include <functional>
#include <memory>
#include <string_view>
#include <vector>

namespace RBX
{
	class Adorn;
}

namespace RBX::Graphics
{
	class AdornRender;
	class Device;
	class IShaderManager;
	class SceneManager;
	class ShaderProgram;
	class VisualEngine;
}

namespace rml::graphics
{
	using AdornCallback = std::function<void(RBX::Adorn&)>;

	RML_EXPORT RBX::Graphics::VisualEngine* visual_engine();
	RML_EXPORT RBX::Graphics::Device* device();
	RML_EXPORT RBX::Graphics::SceneManager* scene_manager();
	RML_EXPORT RBX::Graphics::IShaderManager* shader_manager();
	RML_EXPORT std::shared_ptr<RBX::Graphics::ShaderProgram> engine_program(std::string_view vertex, std::string_view fragment);
	RML_EXPORT void add_adorn_callback(AdornCallback callback);
	RML_EXPORT RBX::Graphics::AdornRender* adorn_render();
	RML_EXPORT std::vector<RBX::Graphics::AdornRender*> adorn_renders();
}
