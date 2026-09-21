--!nocheck

local plugins = require("@self/lib/plugins")

local function load(root)
	local req = function(path) return plugins.requireUnder(root, path) end
	local Framework = req("Packages.Framework")
	return {
		root = root,
		plugin = root.Parent,
		React = req("Packages.React"),
		Localization = Framework.ContextServices.Localization,
		Pages = req("Src.Foundation.Components.Pages"),
		HomePage = req("Src.Foundation.Components.HomePage"),
		HomeSectionHeader = req("Src.Foundation.Components.HomeSectionHeader"),
		PageHeader = req("Src.Foundation.Components.PageHeader"),
	}
end

return { load = load }
