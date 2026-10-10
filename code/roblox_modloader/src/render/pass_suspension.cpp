#include "render/pass_suspension.hpp"

#include "RobloxModLoader/roblox/graphics/device_context.hpp"
#include "RobloxModLoader/roblox/graphics/texture.hpp"

namespace rml::render::detail
{
	unsigned PassSuspension::record(RBX::Graphics::Framebuffer* framebuffer, unsigned store_mask, const RBX::Graphics::PassResolve* resolve, const unsigned flags)
	{
		const bool resolves = resolve && resolve->mask != 0;
		if (resolves && m_widen.load(std::memory_order_acquire))
			store_mask |= resolve->mask;
		m_open = {framebuffer, store_mask, flags, resolves, resolves ? *resolve : RBX::Graphics::PassResolve{}};
		return store_mask;
	}

	void PassSuspension::set_widening(const bool widen)
	{
		m_widen.store(widen, std::memory_order_release);
	}

	std::optional<OpenPass> PassSuspension::suspend(RBX::Graphics::DeviceContext& context, RBX::Graphics::Framebuffer* target)
	{
		std::optional<OpenPass> open;
		if (m_open.framebuffer == target)
			open = m_open;
		context.end_pass();
		return open;
	}

	void PassSuspension::resume(RBX::Graphics::DeviceContext& context, RBX::Graphics::Framebuffer* target, const std::optional<OpenPass>& pass)
	{
		if (pass)
			context.begin_pass(target, pass->store_mask, pass->store_mask, nullptr, pass->resolves ? &pass->resolve : nullptr, pass->flags);
		else
			context.begin_pass(target, target->mask, target->mask, nullptr, nullptr, 0);
	}
}
