--!nocheck

local ctx = require("@self/ribbon/context")

return function()
	local createElement = ctx.m.React.createElement
	ctx.state.onReactHook = function(enabled)
		if enabled then
			local original
			original = ctx.session:hook(createElement, function(...)
				ctx.session.stats.elements = (ctx.session.stats.elements or 0) + 1
				return original(...)
			end)
			ctx.log.info("hooked the Ribbon's React.createElement")
		else
			ctx.session:unhook(createElement)
			ctx.log.info("restored React.createElement")
		end
	end
end
