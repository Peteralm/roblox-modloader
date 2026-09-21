--!nocheck

local ctx = require("@self/explorer/context")
local rows = require("@self/explorer/rows")
local boundary = require("@self/lib/boundary")

local function color(kind)
	return settings().Studio.Theme:GetColor(Enum.StudioStyleGuideColor[kind])
end

local function forVisibleRoots(fn)
	local session = ctx.state.session
	if not session then
		ctx.log.warn("no explorer session captured yet")
		return 0
	end
	local count = 0
	for _, node in ipairs(rows.visibleNodes()) do
		if node.depth == 0 and node.datum.hasChildren then
			local ok, err = pcall(fn, session, node.datum.id)
			if ok then
				count += 1
			else
				ctx.log.warn("session call failed for %s: %s", tostring(node.datum.name), tostring(err))
			end
		end
	end
	return count
end

local actions = {
	{ text = "Expand all", run = function() return forVisibleRoots(function(s, id) s.expandAll({ id }) end) end },
	{ text = "Collapse all", run = function() return forVisibleRoots(function(s, id) s.collapseAll({ id }) end) end },
	{ text = "Classes", toggle = "showClasses" },
}

local function install()
	local m = ctx.m
	local React = m.React
	local e = React.createElement
	local guarded = boundary(React, ctx.log, nil)

	local function Toolbar()
		local version, bump = React.useState(0)
		local buttons = {
			Layout = e("UIListLayout", { FillDirection = Enum.FillDirection.Horizontal, SortOrder = Enum.SortOrder.LayoutOrder, Padding = UDim.new(0, 4), VerticalAlignment = Enum.VerticalAlignment.Center }),
			Padding = e("UIPadding", { PaddingLeft = UDim.new(0, 6), PaddingRight = UDim.new(0, 6), PaddingTop = UDim.new(0, 4), PaddingBottom = UDim.new(0, 4) }),
			Corner = e("UICorner", { CornerRadius = UDim.new(0, 6) }),
			Stroke = e("UIStroke", { Color = color("Border"), Thickness = 1 }),
		}
		for index, action in ipairs(actions) do
			local active = action.toggle and ctx.state[action.toggle]
			buttons[action.text] = e("TextButton", {
				LayoutOrder = index,
				Text = action.text,
				AutomaticSize = Enum.AutomaticSize.XY,
				Size = UDim2.fromOffset(0, 22),
				BackgroundColor3 = active and color("DialogMainButton") or color("Button"),
				TextColor3 = active and color("DialogMainButtonText") or color("ButtonText"),
				BorderSizePixel = 0,
				Font = Enum.Font.BuilderSans,
				TextSize = 13,
				[React.Event.Activated] = function()
					if action.toggle then
						ctx.state[action.toggle] = not ctx.state[action.toggle]
						ctx.log.info("%s -> %s (re-mount the explorer to apply everywhere)", action.toggle, tostring(ctx.state[action.toggle]))
						if ctx.state.remount then
							ctx.state.remount()
						end
					else
						local count = action.run()
						ctx.log.info("%s applied to %d root(s)", action.text, count)
					end
					bump(version + 1)
				end,
			}, { Padding = e("UIPadding", { PaddingLeft = UDim.new(0, 8), PaddingRight = UDim.new(0, 8) }), Corner = e("UICorner", { CornerRadius = UDim.new(0, 4) }) })
		end
		return e("Frame", {
			AnchorPoint = Vector2.new(1, 1),
			Position = UDim2.new(1, -10, 1, -10),
			AutomaticSize = Enum.AutomaticSize.XY,
			Size = UDim2.fromOffset(0, 0),
			BackgroundColor3 = color("MainBackground"),
			BackgroundTransparency = 0.05,
			BorderSizePixel = 0,
			ZIndex = 50,
		}, buttons)
	end

	local Guarded = guarded("ExplorerToolbar", Toolbar)
	local original
	original = ctx.session:hook(m.MainView, function(props)
		local element = original(props)
		local ok, result = pcall(function()
			local frame = element.props.children
			local children = table.clone(frame.props.children)
			children.RmlToolbar = e(Guarded, {})
			return React.cloneElement(element, nil, React.cloneElement(frame, nil, children))
		end)
		if not ok then
			if not ctx.state.toolbarWarned then
				ctx.state.toolbarWarned = true
				ctx.log.warn("toolbar injection failed: %s", tostring(result))
			end
			return element
		end
		return result
	end)
end

local function run(text)
	for _, action in ipairs(actions) do
		if action.text == text and action.run then
			return action.run()
		end
	end
	return nil
end

return { install = install, run = run }
