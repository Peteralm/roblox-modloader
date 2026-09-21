--!nocheck

local watchers = {}
local loop = nil
local frames = 0

local function matches(seen, required, forbidden)
	for _, name in ipairs(required) do
		if not seen[name] then
			return false
		end
	end
	for _, name in ipairs(forbidden or {}) do
		if seen[name] then
			return false
		end
	end
	return true
end

local function constantsOf(fn)
	local seen = {}
	for _, c in pairs(debug.getconstants(fn)) do
		if type(c) == "string" then
			seen[c] = true
		end
	end
	return seen
end

local function filter(list, required, forbidden)
	local out = {}
	for _, fn in ipairs(list or {}) do
		if matches(constantsOf(fn), required, forbidden) then
			out[#out + 1] = fn
		end
	end
	return out
end

local function rootFrom(fn, pluginName)
	local ok, env = pcall(getfenv, fn)
	local script = ok and env and rawget(env, "script")
	if typeof(script) == "Instance" then
		return script:FindFirstAncestor(pluginName)
	end
	return nil
end

local function ready(watcher)
	watcher.done = true
	local ok, err = pcall(watcher.onReady, watcher.root, watcher.found or {})
	if not ok then
		watcher.log.warn("%s setup failed: %s", watcher.name, tostring(err))
	end
end

local function needsHeap(watcher)
	return not watcher.done and (watcher.root == nil or type(watcher.mount) == "table")
end

local function scan()
	local pending = {}
	for _, watcher in ipairs(watchers) do
		if needsHeap(watcher) then
			pending[#pending + 1] = watcher
			watcher.found = {}
		end
	end
	if #pending == 0 then
		return
	end
	for _, value in ipairs(getgc()) do
		if type(value) == "function" and islclosure(value) then
			local src = debug.getinfo(value).short_src
			if src then
				for _, watcher in ipairs(pending) do
					if not watcher.root and src:find(watcher.pattern) then
						watcher.root = rootFrom(value, watcher.name)
					end
					if watcher.sources[src] then
						local list = watcher.found[src]
						if not list then
							list = {}
							watcher.found[src] = list
						end
						list[#list + 1] = value
					end
				end
			end
		end
	end
	for _, watcher in ipairs(pending) do
		if watcher.root and not watcher.announced then
			watcher.announced = true
			watcher.log.info("found %s under %s (%s)", watcher.root:GetFullName(), watcher.root.Parent.Name, watcher.root.Parent.ClassName)
		end
		if watcher.root and type(watcher.mount) == "table" then
			if #filter(watcher.found[watcher.mount.source], watcher.mount.required, watcher.mount.forbidden) > 0 then
				ready(watcher)
			end
		end
	end
end

local function settle()
	for _, watcher in ipairs(watchers) do
		if not watcher.done and watcher.root and type(watcher.mount) ~= "table" then
			local ok, mounted = pcall(function()
				return watcher.mount == nil or watcher.mount(watcher.root)
			end)
			if ok and mounted then
				ready(watcher)
			end
		end
	end
end

local function idle()
	for _, watcher in ipairs(watchers) do
		if not watcher.done then
			return false
		end
	end
	return true
end

local function ensureLoop()
	if loop then
		return
	end
	loop = game:GetService("RunService").Heartbeat:Connect(function()
		frames += 1
		if frames % 60 ~= 0 then
			return
		end
		local scanning = frames < 60 * 120
		local ok, err = pcall(function()
			if scanning then
				scan()
			end
			settle()
		end)
		if not ok then
			watchers[1].log.warn("plugin discovery failed: %s", tostring(err))
		end
		if not scanning then
			for _, watcher in ipairs(watchers) do
				if needsHeap(watcher) and not watcher.gaveUp then
					watcher.gaveUp = true
					watcher.log.warn("gave up looking for %s on the heap", watcher.name)
				end
			end
		end
		if idle() then
			loop:Disconnect()
			loop = nil
		end
	end)
end

local function watch(spec, onReady)
	local sources = {}
	for _, source in ipairs(spec.sources or {}) do
		sources[source] = true
	end
	if type(spec.mount) == "table" then
		sources[spec.mount.source] = true
	end
	table.insert(watchers, {
		name = spec.name,
		pattern = ("%%f[%%w_]%s%%."):format(spec.name),
		sources = sources,
		mount = spec.mount,
		log = spec.log,
		onReady = onReady,
	})
	ensureLoop()
end

local function closures(source, required, forbidden)
	local list = {}
	for _, value in ipairs(getgc()) do
		if type(value) == "function" and islclosure(value) and debug.getinfo(value).short_src == source then
			list[#list + 1] = value
		end
	end
	return filter(list, required, forbidden)
end

local function requireUnder(root, path)
	local node = root
	for part in path:gmatch("[^%.]+") do
		node = node:FindFirstChild(part)
		if not node then
			error(("%s: module %s is missing at %s"):format(root.Name, path, part))
		end
	end
	return require(node)
end

return {
	watch = watch,
	filter = filter,
	closures = closures,
	requireUnder = requireUnder,
}
