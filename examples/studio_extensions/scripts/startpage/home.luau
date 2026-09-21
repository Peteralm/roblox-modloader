--!nocheck

local ctx = require("@self/startpage/context")
local boundary = require("@self/lib/boundary")

local function install()
	local m = ctx.m
	local React = m.React
	local e = React.createElement
	local cards = require("@self/startpage/cards")()
	local guarded = boundary(React, ctx.log, nil)

	local function Section(props)
		return e("Frame", {
			LayoutOrder = props.LayoutOrder,
			Size = UDim2.new(1, 0, 0, 0),
			AutomaticSize = Enum.AutomaticSize.Y,
			BackgroundTransparency = 1,
		}, {
			Layout = e("UIListLayout", { FillDirection = Enum.FillDirection.Vertical, SortOrder = Enum.SortOrder.LayoutOrder, Padding = UDim.new(0, 12) }),
			Header = e(m.HomeSectionHeader, {
				title = "Mod loader",
				description = ("%s %s is running inside this Studio"):format(rml.mod.name, rml.mod.version),
				buttonText = props.onPageSwap and "Open Mods" or nil,
				onButtonClick = props.onPageSwap and function() props.onPageSwap("Mods") end or nil,
				LayoutOrder = 1,
			}),
			Cards = e(cards.CardRow, { LayoutOrder = 2 }),
		})
	end
	local GuardedSection = guarded("HomeSection", Section)

	local function inject(element, props)
		local content = element.props.children.Content
		local children = table.clone(content.props.children)
		local anchor = children.Templates or children.RecentExperiences
		local order = (anchor and anchor.props.LayoutOrder or 0) + 1
		for key, child in pairs(children) do
			if type(child) == "table" and child.props and type(child.props.LayoutOrder) == "number" and child.props.LayoutOrder >= order then
				children[key] = React.cloneElement(child, { LayoutOrder = child.props.LayoutOrder + 1 })
			end
		end
		children.RmlSection = e(GuardedSection, { LayoutOrder = order, onPageSwap = props.onPageSwap })
		local rootChildren = table.clone(element.props.children)
		rootChildren.Content = React.cloneElement(content, nil, children)
		return React.cloneElement(element, nil, rootChildren)
	end

	local original
	original = ctx.session:hook(m.HomePage, function(props)
		local element = original(props)
		local ok, result = pcall(inject, element, props)
		if not ok then
			if not ctx.state.homeWarned then
				ctx.state.homeWarned = true
				ctx.log.warn("home section injection failed: %s", tostring(result))
			end
			return element
		end
		return result
	end)
end

return { install = install }
