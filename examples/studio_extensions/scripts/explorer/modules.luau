--!nocheck

local plugins = require("@self/lib/plugins")

local function load(root)
	local pkg = root.Packages._Index.Explorer.Explorer
	return {
		root = root,
		plugin = root.Parent,
		React = plugins.requireUnder(root, "Packages.React"),
		pkg = pkg,
		ExplorerNodeRow = require(pkg.Components.ExplorerNodeRow),
		MainView = require(pkg.Components.MainView),
	}
end

local function widget()
	for _, tagged in ipairs(game:GetService("CollectionService"):GetTagged("Explorer-BG-Surface100")) do
		local gui = tagged:FindFirstAncestorWhichIsA("DockWidgetPluginGui")
		if gui then
			return gui
		end
	end
	return nil
end

return {
	load = load,
	widget = widget,
}
