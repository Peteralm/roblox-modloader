--!nocheck

local function say(text)
	print("[modthing] " .. text)
	if rml and rml.log then
		pcall(rml.log.info, "[modthing] " .. text)
	end
end

local ok, result = pcall(function()
	local thing = Instance.new("ModThing")
	local lines = {
		"ClassName=" .. thing.ClassName,
		"IsA(Instance)=" .. tostring(thing:IsA("Instance")),
		"IsA(ModThing)=" .. tostring(thing:IsA("ModThing")),
		"IsA(Folder)=" .. tostring(thing:IsA("Folder")),
	}
	thing.Name = "FromLuau"
	thing.Parent = workspace
	table.insert(lines, "Parent=" .. tostring(thing.Parent))
	local found = workspace:FindFirstChild("FromLuau")
	table.insert(lines, "FindFirstChild=" .. tostring(found and found.ClassName))
	return table.concat(lines, " ")
end)

say(ok and ("pass " .. result) or ("FAIL " .. tostring(result)))
