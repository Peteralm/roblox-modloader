--!nocheck

local ctx = require("@self/ribbon/context")
local registry = require("@self/ribbon/actions")
local boundary = require("@self/lib/boundary")

local TEXT_TAG = "Role-Surface Role-Text X-Fit Text-Label"
local TITLE_TAG = "Role-Surface Role-Text X-Fit Text-Title"
local BOX_TAG = "Role-TextControl Role-Surface X-Fit"

return function()
	local m = ctx.m
	local React = m.React
	local e = React.createElement
	local guarded = boundary(React, ctx.log, TEXT_TAG)

	local function RmlButton(props)
		local item = props.Item
		local onSelect = React.useCallback(function()
			local action = registry.actions[item.Id]
			if action then
				local ok, err = pcall(action)
				if not ok then
					props.Warn(("RML action %s failed: %s"):format(item.Id, tostring(err)))
				end
			end
		end, { item.Id })
		return e(m.RibbonButton, {
			Uri = m.StudioUri.child(props.WidgetUri, item.Id),
			Icon = item.Icon,
			Text = item.Text,
			ShortTitle = item.Text,
			Tooltip = item.Tooltip,
			Size = item.Size or "Large",
			IconOnly = item.IconOnly or props.HideLabels,
			LayoutOrder = props.LayoutOrder,
			Selected = registry.selected(item.Id),
			Disabled = false,
			Visible = true,
			OnSelect = onSelect,
			ShowContextMenu = props.ShowContextMenu,
			ControlIndices = props.ControlIndices,
		})
	end

	local function line(order, text, tag)
		return e("TextLabel", {
			LayoutOrder = order,
			Text = text,
			[React.Tag] = tag or TEXT_TAG,
			BackgroundTransparency = 1,
			AutomaticSize = Enum.AutomaticSize.XY,
			Size = UDim2.fromOffset(0, 0),
			TextXAlignment = Enum.TextXAlignment.Left,
		})
	end

	local function box(props, children)
		return e("Frame", {
			LayoutOrder = props.LayoutOrder,
			[React.Tag] = BOX_TAG,
			BackgroundTransparency = 1,
			AutomaticSize = Enum.AutomaticSize.XY,
			Size = UDim2.fromOffset(0, 0),
		}, children)
	end

	local function RmlStats(props)
		local stats = ctx.session.stats
		local snapshot, setSnapshot = React.useState(stats)
		React.useEffect(function()
			local RunService = game:GetService("RunService")
			local Stats = game:GetService("Stats")
			local frames, elapsed = 0, 0
			local conn = RunService.Heartbeat:Connect(function(dt)
				frames += 1
				elapsed += dt
				if elapsed < 1 then
					return
				end
				stats.fps = math.floor(frames / elapsed + 0.5)
				stats.memory = math.floor(Stats:GetTotalMemoryUsageMb())
				stats.luauMb = math.floor(collectgarbage("count") / 1024)
				stats.elementsPerSecond = math.floor((stats.elements or 0) / elapsed)
				stats.elements = 0
				frames, elapsed = 0, 0
				setSnapshot(table.clone(stats))
			end)
			return function()
				conn:Disconnect()
			end
		end, {})
		return box(props, {
			Layout = e("UIListLayout", { FillDirection = Enum.FillDirection.Vertical, SortOrder = Enum.SortOrder.LayoutOrder, Padding = UDim.new(0, 2) }),
			Padding = e("UIPadding", { PaddingLeft = UDim.new(0, 6), PaddingRight = UDim.new(0, 6), PaddingTop = UDim.new(0, 2) }),
			Fps = line(1, ("%s fps · %s MB · Luau %s MB"):format(tostring(snapshot.fps or "-"), tostring(snapshot.memory or "-"), tostring(snapshot.luauMb or "-"))),
			Hooks = line(2, ("%d hooks · React %s el/s"):format(ctx.session.count, ctx.state.reactHooked and tostring(snapshot.elementsPerSecond or 0) or "off")),
			Heap = line(3, snapshot.heap or "getgc: press Heap Scan"),
		})
	end

	local function RmlBadge(props)
		return box(props, {
			Padding = e("UIPadding", { PaddingLeft = UDim.new(0, 8), PaddingRight = UDim.new(0, 8) }),
			Text = line(1, props.Item.Text, TITLE_TAG),
		})
	end

	m.Renderers.RmlButton = guarded("RmlButton", RmlButton)
	m.Renderers.RmlStats = guarded("RmlStats", RmlStats)
	m.Renderers.RmlBadge = guarded("RmlBadge", RmlBadge)
end
