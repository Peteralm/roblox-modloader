--!nocheck

local ctx = require("@self/ribbon/context")
local registry = require("@self/ribbon/actions")

return function()
	task.spawn(function()
		local passed, failed = 0, 0
		for _, id in ipairs({ "RmlPlayground", "RmlHeap", "RmlDump", "RmlHookReact", "RmlReload", "RmlHookReact" }) do
			local ok, err = pcall(registry.actions[id])
			if ok then
				passed += 1
			else
				failed += 1
				ctx.log.warn("selfcheck %s failed: %s", id, tostring(err))
			end
			task.wait(0.5)
		end
		ctx.log.info("selfcheck done: %d passed, %d failed", passed, failed)
	end)
end
