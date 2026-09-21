--!nocheck

local history = {}
local LIMIT = 60

local function record(line)
	table.insert(history, line)
	if #history > LIMIT then
		table.remove(history, 1)
	end
end

local module = {}

function module.history()
	return history
end

return setmetatable(module, {
	__call = function(_, scope)
		local prefix = ("[%s] "):format(scope)
		local function emit(level, fmt, ...)
			local text = select("#", ...) > 0 and string.format(fmt, ...) or tostring(fmt)
			print(prefix .. text)
			record(os.date("%H:%M:%S ") .. prefix .. text)
			pcall(rml.log[level], prefix .. text)
		end
		return {
			info = function(fmt, ...) emit("info", fmt, ...) end,
			warn = function(fmt, ...) emit("warn", fmt, ...) end,
		}
	end,
})
