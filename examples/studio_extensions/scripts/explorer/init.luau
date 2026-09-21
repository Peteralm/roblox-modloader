--!nocheck

local ctx = require("@self/explorer/context")
local plugins = require("@self/lib/plugins")
local Session = require("@self/lib/session")
local modules = require("@self/explorer/modules")

local function remount()
	local widget = modules.widget()
	if not widget or not widget.Enabled then
		return false
	end
	task.spawn(function()
		widget.Enabled = false
		task.wait()
		widget.Enabled = true
	end)
	return true
end

return function()
	ctx.log.info("waiting for the ExplorerPlugin")
	plugins.watch({
		name = "ExplorerPlugin",
		log = ctx.log,
		mount = function() return modules.widget() ~= nil end,
	}, function(root)
		ctx.m = modules.load(root)
		ctx.session = Session.attach(ctx.m.React, "__rmlExplorer", ctx.log)
		ctx.state.showClasses = true
		ctx.state.remount = remount
		require("@self/explorer/rows").install()
		require("@self/explorer/toolbar").install()
		ctx.log.info("ready: %d hooks installed, re-mounting the widget: %s", ctx.session.count, tostring(remount()))
	end)
end
