--!nocheck

local ctx = require("@self/startpage/context")
local plugins = require("@self/lib/plugins")
local Session = require("@self/lib/session")
local modules = require("@self/startpage/modules")

return function()
	ctx.log.info("waiting for the StartPage")
	plugins.watch({ name = "StartPage", log = ctx.log }, function(root)
		ctx.m = modules.load(root)
		ctx.session = Session.attach(ctx.m.React, "__rmlStartPage", ctx.log)
		require("@self/startpage/pages").install()
		require("@self/startpage/home").install()
		ctx.log.info("ready: %d hooks installed", ctx.session.count)
	end)
end
