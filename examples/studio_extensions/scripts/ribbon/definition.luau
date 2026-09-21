--!nocheck

local ctx = require("@self/ribbon/context")

local registry = require("@self/ribbon/actions")

local TAB_IDENTIFIER = { Type = "Custom", Filename = "RML_ribbon_extensions" }
local HOME_KEY = "BuiltIn_HomeTab"
local MEZZANINES = { "BuiltIn_RightMezzanine", "BuiltIn_RightMezzanineStartPage" }
local MARK = "RmlInjected"

local function studioAction(pluginId, itemId)
	return { PluginId = pluginId, DataModel = "Standalone", ItemId = itemId or "Toggle", Category = "Actions" }
end

local function tabControls()
	return {
		{ Id = "RmlReload", Type = "RmlButton", Icon = "Setup", Text = "Reload\nRibbon", Tooltip = "Re-run the Ribbon definition loader through its own closure" },
		{ Id = "RmlDump", Type = "RmlButton", Icon = "Output", Text = "Dump\nTabs", Tooltip = "Log every tab and control of the live definition" },
		{ Id = "RmlHeap", Type = "RmlButton", Icon = "Find", Text = "Heap\nScan", Tooltip = "Walk the Luau heap with getgc" },
		{ Type = "Separator", Size = "Large" },
		{ Id = "RmlPlayground", Type = "RmlButton", Icon = "Script", Text = "Playground", Tooltip = "Run scripts/playground.luau (hot reloads on save)" },
		{ Id = "RmlHookReact", Type = "RmlButton", Icon = "ReactDevTools", Text = "Hook\nReact", Tooltip = "hookfunction React.createElement and count elements" },
		{ Id = "RmlSelfCheck", Type = "RmlButton", Icon = "Analysis", Text = "Self\nCheck", Tooltip = "Run every RML action from an engine thread" },
		{ Type = "Separator", Size = "Large" },
		{ Id = "Output", Type = "IconButton", Action = studioAction("Output") },
		{ Id = "Performance", Type = "IconButton", Action = studioAction("Performance") },
		{ Id = "Toolbox", Type = "IconButton", Action = studioAction("Toolbox") },
		{ Type = "Separator", Size = "Large" },
		{ Id = "RmlStats", Type = "RmlStats", Text = "Mod loader" },
	}
end

local function stripped(controls, prefix)
	local out = {}
	for _, control in ipairs(controls) do
		if not (type(control) == "table" and control[MARK]) then
			table.insert(out, control)
		end
	end
	return out
end

local function withControls(container, controls)
	local copy = table.clone(container)
	copy.Controls = controls
	return copy
end

local function appendMarked(list, control)
	control[MARK] = true
	table.insert(list, control)
end

local function inject(def, m)
	local key = m.controlsIdentifierToString(TAB_IDENTIFIER)
	local tabs = def.TabControls
	local layout = def.Layout and def.Layout.TabLayout
	if type(tabs) ~= "table" or type(layout) ~= "table" then
		return false
	end

	local controls = tabControls()
	for _, control in ipairs(controls) do
		control[MARK] = true
	end
	tabs[key] = { Name = "RML", Controls = controls }

	local present = false
	for index, entry in ipairs(layout) do
		if entry.Identifier and entry.Identifier.Filename == TAB_IDENTIFIER.Filename then
			present = true
			local copy = table.clone(entry)
			copy.Visible = true
			layout[index] = copy
		end
	end
	if not present then
		table.insert(layout, { Identifier = table.clone(TAB_IDENTIFIER), Visible = true })
	end

	local home = tabs[HOME_KEY]
	if home and type(home.Controls) == "table" then
		local list = stripped(home.Controls)
		appendMarked(list, { Type = "Separator", Size = "Large" })
		appendMarked(list, { Id = "RmlHomePlayground", Type = "RmlButton", Icon = "Script", Text = "Playground", Tooltip = "Injected by Ribbon Extensions", Size = "Large" })
		tabs[HOME_KEY] = withControls(home, list)
	end

	if type(def.MezzanineControls) == "table" then
		local mezzanines = table.clone(def.MezzanineControls)
		for _, name in ipairs(MEZZANINES) do
			local group = mezzanines[name]
			if group and type(group.Controls) == "table" then
				local list = stripped(group.Controls)
				table.insert(list, 1, { Id = "RmlBadge", Type = "RmlBadge", Text = ("RML %s"):format(rml.mod.version), [MARK] = true })
				mezzanines[name] = withControls(group, list)
			end
		end
		def.MezzanineControls = mezzanines
	end
	return true
end

local function strip(def, m)
	if type(def) ~= "table" or type(def.TabControls) ~= "table" then
		return def
	end
	local key = m.controlsIdentifierToString(TAB_IDENTIFIER)
	local copy = table.clone(def)
	local tabs = table.clone(def.TabControls)
	tabs[key] = nil
	for name, tab in pairs(tabs) do
		if type(tab) == "table" and type(tab.Controls) == "table" then
			local tabCopy = table.clone(tab)
			tabCopy.Controls = stripped(tab.Controls)
			tabs[name] = tabCopy
		end
	end
	copy.TabControls = tabs
	if type(def.Layout) == "table" and type(def.Layout.TabLayout) == "table" then
		local layoutCopy = table.clone(def.Layout)
		local entries = {}
		for _, entry in ipairs(def.Layout.TabLayout) do
			if not (entry.Identifier and entry.Identifier.Filename == TAB_IDENTIFIER.Filename) then
				table.insert(entries, entry)
			end
		end
		layoutCopy.TabLayout = entries
		copy.Layout = layoutCopy
	end
	if type(def.MezzanineControls) == "table" then
		local groups = table.clone(def.MezzanineControls)
		for name, group in pairs(groups) do
			if type(group) == "table" and type(group.Controls) == "table" then
				local groupCopy = table.clone(group)
				groupCopy.Controls = stripped(group.Controls)
				groups[name] = groupCopy
			end
		end
		copy.MezzanineControls = groups
	end
	return copy
end

local function isDefinition(value)
	return type(value) == "table" and type(rawget(value, "TabControls")) == "table" and type(rawget(value, "Layout")) == "table"
end

local function installLoadHook(m)
	local original
	original = ctx.session:hook(m.deepFreeze, function(value, ...)
		if isDefinition(value) and not table.isfrozen(value) then
			local ok, err = pcall(inject, value, m)
			if ok then
				ctx.state.lastDefinition = value
				ctx.log.info("injected the RML tab into a definition with %d layout entries", #value.Layout.TabLayout)
			else
				ctx.log.warn("injection failed: %s", tostring(err))
			end
		end
		return original(value, ...)
	end)
end

local function installSaveGuard(m, modules, providerClosures)
	local schedulers = modules.schedulerClosures(providerClosures)
	local guarded = 0
	for _, fn in ipairs(schedulers) do
		local ref = debug.getupvalue(fn, 1)
		local current = type(ref) == "table" and ref.current
		if type(current) == "table" then
			local real = current
			local meta = getmetatable(real)
			if meta and meta.__rml then
				real = meta.__rml
			end
			local proxy = setmetatable({}, {
				__rml = real,
				__index = function(_, k)
					local v = real[k]
					if k == "LatestDefinition" then
						return strip(v, m)
					end
					return v
				end,
				__newindex = function(_, k, v)
					real[k] = v
				end,
			})
			ref.current = proxy
			ctx.session:onCleanup(function()
				if ref.current == proxy then
					ref.current = real
				end
			end)
			guarded += 1
		end
	end
	ctx.log.info("guarded %d save scheduler(s) against persisting the RML tab", guarded)
end

local function dump()
	local def = ctx.state.lastDefinition
	if not def then
		ctx.log.warn("no definition captured yet")
		return
	end
	for index, entry in ipairs(def.Layout.TabLayout) do
		local id = entry.Identifier
		local key = ("%s_%s"):format(id.Type, id.Filename)
		local tab = def.TabControls[key]
		local ids = {}
		for _, control in ipairs(tab and tab.Controls or {}) do
			table.insert(ids, control.Id or control.Type)
		end
		ctx.log.info("tab %d %s (%s) visible=%s: %s", index, tab and tab.Name or "?", key, tostring(entry.Visible), table.concat(ids, ", "))
	end
	for name, group in pairs(def.MezzanineControls or {}) do
		local ids = {}
		for _, control in ipairs(group.Controls or {}) do
			table.insert(ids, control.Id or control.Type)
		end
		ctx.log.info("mezzanine %s: %s", name, table.concat(ids, ", "))
	end
end

return function(modules, providerClosures)
	local m = ctx.m
	installLoadHook(m)
	installSaveGuard(m, modules, providerClosures)
	ctx.state.dump = dump
	ctx.state.reload = function()
		local loader = modules.reloadClosure(providerClosures)
		providerClosures = nil
		if not loader then
			ctx.log.warn("the provider loader closure was not found on the heap")
			return
		end
		local ok, err = pcall(loader)
		ctx.log.info("definition loader re-run ok=%s %s", tostring(ok), ok and "" or tostring(err))
	end
	ctx.state.reload()
end
