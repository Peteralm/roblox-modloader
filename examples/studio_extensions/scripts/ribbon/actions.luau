--!nocheck

local ctx = require("@self/ribbon/context")

local actions = {}

local function runPlayground()
	local ok, result = pcall(require, "@self/ribbon/playground")
	if not ok then
		ctx.log.warn("playground failed to load: %s", tostring(result))
		return
	end
	if type(result) == "function" then
		local ran, err = pcall(result)
		if not ran then
			ctx.log.warn("playground raised: %s", tostring(err))
		end
	else
		ctx.log.info("playground returned %s", tostring(result))
	end
end

local function heapScan()
	local functions, tables, instances, ribbon = 0, 0, 0, 0
	for _, v in ipairs(getgc(true)) do
		local t = type(v)
		if t == "function" then
			functions += 1
			if islclosure(v) then
				local info = debug.getinfo(v)
				if info.short_src and info.short_src:find("^Ribbon%.Ribbon%.") then
					ribbon += 1
				end
			end
		elseif t == "table" then
			tables += 1
		elseif typeof(v) == "Instance" then
			instances += 1
		end
	end
	ctx.session.stats.heap = ("%d fn / %d tbl / %d inst"):format(functions, tables, instances)
	ctx.log.info("heap: %d functions (%d from the Ribbon), %d tables, %d instances", functions, ribbon, tables, instances)
end

actions.RmlReload = function()
	if ctx.state.reload then
		ctx.state.reload()
	end
end
actions.RmlDump = function()
	if ctx.state.dump then
		ctx.state.dump()
	end
end
actions.RmlPlayground = runPlayground
actions.RmlHomePlayground = runPlayground
actions.RmlHeap = heapScan
actions.RmlSelfCheck = function()
	require("@self/ribbon/selfcheck")()
end
actions.RmlHookReact = function()
	ctx.state.reactHooked = not ctx.state.reactHooked
	if ctx.state.onReactHook then
		ctx.state.onReactHook(ctx.state.reactHooked)
	end
end

return {
	actions = actions,
	selected = function(id)
		if id == "RmlHookReact" then
			return ctx.state.reactHooked
		end
		return false
	end,
}
