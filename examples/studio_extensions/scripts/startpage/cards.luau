--!nocheck

local ctx = require("@self/startpage/context")
local ribbon = require("@self/ribbon/context")
local explorer = require("@self/explorer/context")

local function color(kind)
	return settings().Studio.Theme:GetColor(Enum.StudioStyleGuideColor[kind])
end

local function entries()
	local list = {
		{ title = rml.mod.name, detail = ("v%s · %s"):format(rml.mod.version, rml.mod.author) },
		{ title = "Ribbon", detail = ribbon.session and ("%d hook(s) · RML tab + Home button + badge"):format(ribbon.session.count) or "waiting", action = "Reload ribbon", run = function() if ribbon.state.reload then ribbon.state.reload() end end },
		{ title = "Explorer", detail = explorer.session and ("%d hook(s) · class labels + toolbar"):format(explorer.session.count) or "waiting", action = "Re-mount", run = function() if explorer.state.remount then explorer.state.remount() end end },
		{ title = "Start page", detail = ctx.session and ("%d hook(s) · Mods page + home section"):format(ctx.session.count) or "waiting" },
	}
	return list
end

return function()
	local React = ctx.m.React
	local e = React.createElement

	local function Card(props)
		local entry = props.entry
		return e("Frame", {
			LayoutOrder = props.LayoutOrder,
			Size = UDim2.fromOffset(230, 96),
			BackgroundColor3 = color("Item"),
			BorderSizePixel = 0,
		}, {
			Corner = e("UICorner", { CornerRadius = UDim.new(0, 8) }),
			Stroke = e("UIStroke", { Color = color("Border"), Thickness = 1 }),
			Padding = e("UIPadding", { PaddingLeft = UDim.new(0, 12), PaddingRight = UDim.new(0, 12), PaddingTop = UDim.new(0, 10), PaddingBottom = UDim.new(0, 10) }),
			Layout = e("UIListLayout", { FillDirection = Enum.FillDirection.Vertical, SortOrder = Enum.SortOrder.LayoutOrder, Padding = UDim.new(0, 4) }),
			Title = e("TextLabel", {
				LayoutOrder = 1,
				Text = entry.title,
				Font = Enum.Font.BuilderSansBold,
				TextSize = 16,
				TextColor3 = color("MainText"),
				BackgroundTransparency = 1,
				AutomaticSize = Enum.AutomaticSize.XY,
				Size = UDim2.fromOffset(0, 0),
				TextXAlignment = Enum.TextXAlignment.Left,
			}),
			Detail = e("TextLabel", {
				LayoutOrder = 2,
				Text = entry.detail,
				Font = Enum.Font.BuilderSans,
				TextSize = 13,
				TextColor3 = color("DimmedText"),
				BackgroundTransparency = 1,
				AutomaticSize = Enum.AutomaticSize.Y,
				Size = UDim2.new(1, 0, 0, 0),
				TextWrapped = true,
				TextXAlignment = Enum.TextXAlignment.Left,
			}),
			Action = entry.action and e("TextButton", {
				LayoutOrder = 3,
				Text = entry.action,
				Font = Enum.Font.BuilderSans,
				TextSize = 13,
				AutomaticSize = Enum.AutomaticSize.XY,
				Size = UDim2.fromOffset(0, 22),
				BackgroundColor3 = color("Button"),
				TextColor3 = color("ButtonText"),
				BorderSizePixel = 0,
				[React.Event.Activated] = function()
					local ok, err = pcall(entry.run)
					if not ok then
						ctx.log.warn("%s failed: %s", entry.action, tostring(err))
					end
				end,
			}, {
				Padding = e("UIPadding", { PaddingLeft = UDim.new(0, 8), PaddingRight = UDim.new(0, 8) }),
				Corner = e("UICorner", { CornerRadius = UDim.new(0, 4) }),
			}) or nil,
		})
	end

	local function CardRow(props)
		local children = {
			Layout = e("UIListLayout", { FillDirection = Enum.FillDirection.Horizontal, SortOrder = Enum.SortOrder.LayoutOrder, Padding = UDim.new(0, 12), Wraps = true }),
		}
		for index, entry in ipairs(entries()) do
			children[entry.title] = e(Card, { entry = entry, LayoutOrder = index })
		end
		return e("Frame", {
			LayoutOrder = props.LayoutOrder,
			Size = UDim2.new(1, 0, 0, 0),
			AutomaticSize = Enum.AutomaticSize.Y,
			BackgroundTransparency = 1,
		}, children)
	end

	return { Card = Card, CardRow = CardRow, color = color }
end
