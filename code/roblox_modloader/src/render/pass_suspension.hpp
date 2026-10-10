#pragma once

#include "RobloxModLoader/roblox/graphics/types.hpp"

#include <atomic>
#include <optional>

namespace RBX::Graphics
{
	class DeviceContext;
	class Framebuffer;
}

namespace rml::render::detail
{
	struct OpenPass
	{
		RBX::Graphics::Framebuffer* framebuffer{};
		unsigned store_mask{};
		unsigned flags{};
		bool resolves{};
		RBX::Graphics::PassResolve resolve{};
	};

	class PassSuspension
	{
	public:
		unsigned record(RBX::Graphics::Framebuffer* framebuffer, unsigned store_mask, const RBX::Graphics::PassResolve* resolve, unsigned flags);
		void set_widening(bool widen);
		[[nodiscard]] std::optional<OpenPass> suspend(RBX::Graphics::DeviceContext& context, RBX::Graphics::Framebuffer* target);
		void resume(RBX::Graphics::DeviceContext& context, RBX::Graphics::Framebuffer* target, const std::optional<OpenPass>& pass);

	private:
		OpenPass m_open;
		std::atomic<bool> m_widen{false};
	};
}
