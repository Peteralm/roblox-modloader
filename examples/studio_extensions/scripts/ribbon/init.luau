--!nocheck

local ctx = require("@self/ribbon/context")
local plugins = require("@self/lib/plugins")
local Session = require("@self/lib/session")
local modules = require("@self/ribbon/modules")

return function()
	ctx.log.info("waiting for the Ribbon plugin")
	plugins.watch({
		name = "Ribbon",
		log = ctx.log,
		mount = { source = modules.PROVIDER, required = modules.RELOAD[1], forbidden = modules.RELOAD[2] },
	}, function(root, found)
		ctx.m = modules.load(root)
		ctx.session = Session.attach(ctx.m.React, "__rmlRibbon", ctx.log)
		require("@self/ribbon/controls")()
		require("@self/ribbon/react_hook")()
		require("@self/ribbon/definition")(modules, found[modules.PROVIDER])
		ctx.log.info("ready: %d hooks installed", ctx.session.count)
	end)
end
