--!nocheck

local plugins = require("@self/lib/plugins")

local PROVIDER = "Ribbon.Ribbon.Src.Components.RibbonDefinitionProvider"
local RELOAD = { { "task", "defer" }, { "cancel", "Disconnect", "delay" } }
local SCHEDULER = { { "LatestDefinition", "Thread", "current" }, { "SaveRibbonDefinitionAsync", "Disconnect" } }

local function load(root)
	local req = function(path) return plugins.requireUnder(root, path) end
	local StudioFoundation = req("Packages.StudioFoundation")
	return {
		root = root,
		plugin = root.Parent,
		React = req("Packages.React"),
		StudioUri = StudioFoundation.Util.StudioUri,
		Renderers = req("Src.Components.ControlsView.Renderers"),
		RibbonButton = req("Src.Components.RibbonButton"),
		deepFreeze = req("Src.Util.deepFreeze"),
		controlsIdentifierToString = req("Src.Util.controlsIdentifierToString"),
	}
end

local function reloadClosure(list)
	local found = plugins.filter(list or plugins.closures(PROVIDER, RELOAD[1], RELOAD[2]), RELOAD[1], RELOAD[2])
	return found[#found]
end

local function schedulerClosures(list)
	return plugins.filter(list or plugins.closures(PROVIDER, SCHEDULER[1], SCHEDULER[2]), SCHEDULER[1], SCHEDULER[2])
end

return {
	PROVIDER = PROVIDER,
	RELOAD = RELOAD,
	load = load,
	reloadClosure = reloadClosure,
	schedulerClosures = schedulerClosures,
}
