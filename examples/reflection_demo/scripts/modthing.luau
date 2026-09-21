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
		"IsA(ModThing)=" .. tostring(thing:IsA("ModThing")),
	}
	thing.Name = "FromLuau"
	thing.Parent = workspace
	thing.Speed = 3.5
	thing.Label = "hello"
	table.insert(lines, "Speed=" .. tostring(thing.Speed))
	table.insert(lines, "Label=" .. tostring(thing.Label))
	local previous = thing:Reset()
	table.insert(lines, "Reset()=" .. tostring(previous))
	table.insert(lines, "Speed after=" .. tostring(thing.Speed))
	table.insert(lines, "Label after=" .. tostring(thing.Label))
	local child = Instance.new("Folder")
	child.Name = "Child"
	child.Parent = thing
	table.insert(lines, "children=" .. tostring(#thing:GetChildren()))
	thing:Destroy()
	table.insert(lines, "destroyed=" .. tostring(thing.Parent == nil))
	return table.concat(lines, " ")
end)

say(ok and ("pass " .. result) or ("FAIL " .. tostring(result)))
