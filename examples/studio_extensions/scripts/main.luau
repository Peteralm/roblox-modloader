--!nocheck

local log = require("@self/lib/log")("studio_ext")

log.info("%s %s starting", rml.mod.name, rml.mod.version)

for _, feature in ipairs({ "ribbon", "explorer", "startpage" }) do
	local ok, err = pcall(function()
		require("@self/" .. feature)()
	end)
	if not ok then
		log.warn("%s failed to start: %s", feature, tostring(err))
	end
end
