--!nocheck

local ctx = require("@self/explorer/context")

local tracked = setmetatable({}, { __mode = "k" })

local function classBinding(observable)
	return observable.getBinding():map(function(node)
		local datum = node and node.datum
		if not datum or not datum.className or datum.className == datum.name then
			return ""
		end
		return datum.className
	end)
end

local function withChildren(React, element, children)
	return React.cloneElement(element, nil, children)
end

local function decorate(React, element, props)
	local outer = element.props.children.Contents
	local inner = outer.props.children.Contents
	local name = inner.props.children.Name
	local label = React.cloneElement(name, {
		Text = classBinding(props.visibleNodeObservable),
		TextTransparency = 0.55,
		LayoutOrder = 1000,
	})
	local innerChildren = table.clone(inner.props.children)
	innerChildren.RmlClass = label
	local outerChildren = table.clone(outer.props.children)
	outerChildren.Contents = withChildren(React, inner, innerChildren)
	return withChildren(React, element, { Contents = withChildren(React, outer, outerChildren) })
end

local function install()
	local m = ctx.m
	local React = m.React
	local row = m.ExplorerNodeRow
	local render = type(row) == "table" and row.type or row
	local original
	original = ctx.session:hook(render, function(props)
		local element = original(props)
		if props.session then
			ctx.state.session = props.session
		end
		if props.visibleNodeObservable then
			tracked[props.visibleNodeObservable] = true
		end
		if not ctx.state.showClasses then
			return element
		end
		local ok, decorated = pcall(decorate, React, element, props)
		if not ok then
			if not ctx.state.decorateWarned then
				ctx.state.decorateWarned = true
				ctx.log.warn("row decoration failed: %s", tostring(decorated))
			end
			return element
		end
		return decorated
	end)
end

local function visibleNodes()
	local nodes = {}
	for observable in pairs(tracked) do
		local ok, value = pcall(function() return observable:get() end)
		if ok and type(value) == "table" and value.datum and value.datum.id ~= nil and value.yPixels and value.yPixels >= 0 then
			nodes[#nodes + 1] = value
		end
	end
	return nodes
end

return {
	install = install,
	visibleNodes = visibleNodes,
}
