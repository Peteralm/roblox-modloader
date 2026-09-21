--!nocheck

local log = require("@self/lib/log")("playground")

return function()
	local workspace = game:GetService("Workspace")
	log.info("playground: DataModel %s, workspace has %d children, %d KB of Luau heap", tostring(game), #workspace:GetChildren(), math.floor(collectgarbage("count")))
end
