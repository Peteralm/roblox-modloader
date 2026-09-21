#include <doctest/doctest.h>

#include "RobloxModLoader/hooking/vtable_index.hpp"
#include "RobloxModLoader/roblox/graphics/device.hpp"
#include "RobloxModLoader/roblox/graphics/device_context.hpp"

#include <string>
#include <type_traits>
#include <vector>

TEST_CASE("graphics interfaces are pure and keep the dumped slot order")
{
	using namespace RBX::Graphics;
	static_assert(std::is_abstract_v<Device>);
	static_assert(std::is_abstract_v<DeviceContext>);
	static_assert(sizeof(Device) == sizeof(void*));
	static_assert(sizeof(DeviceContext) == sizeof(void*));
	static_assert(static_cast<int>(Texture::Format::RGBA8) == 4);
	static_assert(static_cast<int>(Texture::Format::BGRA8) == 5);
	static_assert(static_cast<int>(Texture::Format::D24S8) == 30);
	static_assert(static_cast<int>(Texture::Format::Count) == 40);

#if !defined(_MSC_VER)
	const std::vector<char> bytecode;
	const std::string name;
	const RasterizerState rasterizer{};
	const BlendState blend{};
	const DepthState depth{};
	CHECK(rml::vtable_index_of(&Device::create_shader, Shader::Type::Vertex, bytecode, name) == 24);
	CHECK(rml::vtable_index_of(&Device::create_texture_impl, Texture::Type::Type_2D, Texture::Format::RGBA8, 0u, 0u, 0u, 0u, 0u, 0u, Texture::Usage::Static, name) == 48);
	CHECK(rml::vtable_index_of(&DeviceContext::set_render_state, rasterizer, blend, depth) == 26);
	CHECK(rml::vtable_index_of(&DeviceContext::end_sync_profiler_scope_unmapped, std::uint64_t{}, nullptr, nullptr) == 39);
	CHECK(rml::vtable_index_of(&Texture::reduce_mip_levels, 0u) == 13);
	CHECK(rml::vtable_index_of(&Buffer::download_debug, 0u, nullptr, 0u) == 7);
	CHECK(rml::vtable_index_of(&Shader::reload, bytecode) == 4);
#endif
}
