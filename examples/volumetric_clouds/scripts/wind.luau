--!nocheck
local world = game:GetService("Workspace")
local tracked = setmetatable({}, { __mode = "k" })

local function push(clouds)
	local wind = world.GlobalWind
	pcall(clouds.SetGlobalWind, clouds, wind.X, wind.Y, wind.Z)
end

local function track(instance)
	if tracked[instance] or instance.ClassName ~= "VolumetricClouds" then
		return
	end
	tracked[instance] = true
	push(instance)
end

for _, container in { world, game:GetService("Lighting") } do
	for _, descendant in container:GetDescendants() do
		track(descendant)
	end
end

game.DescendantAdded:Connect(track)
world:GetPropertyChangedSignal("GlobalWind"):Connect(function()
	for clouds in tracked do
		push(clouds)
	end
end)
