--!nocheck

local ctx = require("@self/startpage/context")
local boundary = require("@self/lib/boundary")
local logs = require("@self/lib/log")

local TEXT_KEY = "PageMenu.Mods"
local ICON = "rbxasset://studio_svg_textures/Shared/Ribbon/Dark/Medium/RibbonSetup.png"

local function install()
	local m = ctx.m
	local React = m.React
	local e = React.createElement
	local cards = require("@self/startpage/cards")()
	local guarded = boundary(React, ctx.log, nil)

	local function ModsPage(props)
		local tick, setTick = React.useState(0)
		React.useEffect(function()
			local conn = game:GetService("RunService").Heartbeat:Connect((function()
				local elapsed = 0
				return function(dt)
					elapsed += dt
					if elapsed >= 2 then
						elapsed = 0
						setTick(function(v) return v + 1 end)
					end
				end
			end)())
			return function() conn:Disconnect() end
		end, {})
		local lines = {}
		local history = logs.history()
		for i = math.max(1, #history - 24), #history do
			lines[#lines + 1] = history[i]
		end
		return e("ScrollingFrame", {
			Size = UDim2.fromScale(1, 1),
			CanvasSize = UDim2.new(),
			AutomaticCanvasSize = Enum.AutomaticSize.Y,
			ScrollBarThickness = 6,
			BackgroundTransparency = 1,
			BorderSizePixel = 0,
		}, {
			Layout = e("UIListLayout", { FillDirection = Enum.FillDirection.Vertical, SortOrder = Enum.SortOrder.LayoutOrder, Padding = UDim.new(0, 16) }),
			Padding = e("UIPadding", { PaddingLeft = UDim.new(0, 24), PaddingRight = UDim.new(0, 24), PaddingTop = UDim.new(0, 8), PaddingBottom = UDim.new(0, 24) }),
			Header = e(m.PageHeader, { title = "Mods", LayoutOrder = 1 }),
			Cards = e(cards.CardRow, { LayoutOrder = 2 }),
			LogTitle = e(m.HomeSectionHeader, { title = "Recent activity", description = ("last %d lines from the mod's log"):format(#lines), LayoutOrder = 3 }),
			Log = e("TextLabel", {
				LayoutOrder = 4,
				Text = table.concat(lines, "\n"),
				Font = Enum.Font.RobotoMono,
				TextSize = 13,
				TextColor3 = cards.color("MainText"),
				BackgroundColor3 = cards.color("Item"),
				BorderSizePixel = 0,
				AutomaticSize = Enum.AutomaticSize.Y,
				Size = UDim2.new(1, 0, 0, 0),
				TextXAlignment = Enum.TextXAlignment.Left,
				TextYAlignment = Enum.TextYAlignment.Top,
				TextWrapped = true,
				RichText = false,
			}, {
				Corner = e("UICorner", { CornerRadius = UDim.new(0, 8) }),
				Padding = e("UIPadding", { PaddingLeft = UDim.new(0, 12), PaddingRight = UDim.new(0, 12), PaddingTop = UDim.new(0, 10), PaddingBottom = UDim.new(0, 10) }),
			}),
		})
	end

	local Page = guarded("ModsPage", ModsPage)

	for index = #m.Pages, 1, -1 do
		if m.Pages[index].TextKey == TEXT_KEY then
			table.remove(m.Pages, index)
		end
	end
	table.insert(m.Pages, { TextKey = TEXT_KEY, PageComponent = Page, Icon = ICON, ActiveIcon = ICON })
	ctx.session:onCleanup(function()
		for index = #m.Pages, 1, -1 do
			if m.Pages[index].TextKey == TEXT_KEY then
				table.remove(m.Pages, index)
			end
		end
	end)

	local originalGetText
	originalGetText = ctx.session:hook(m.Localization.getText, function(self, namespace, key, ...)
		if key == TEXT_KEY then
			return "Mods"
		end
		return originalGetText(self, namespace, key, ...)
	end)
	ctx.log.info("registered the Mods page (%d pages)", #m.Pages)
end

return { install = install }
