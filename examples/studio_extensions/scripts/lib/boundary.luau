--!nocheck

return function(React, log, tag)
	local Boundary = React.Component:extend("RmlBoundary")

	function Boundary:init()
		self:setState({ failure = nil })
	end

	function Boundary.getDerivedStateFromError(err)
		return { failure = tostring(err) }
	end

	function Boundary:componentDidCatch(err)
		log.warn("%s crashed: %s", tostring(self.props.name), tostring(err))
	end

	function Boundary:render()
		if self.state.failure then
			if not tag then
				return nil
			end
			return React.createElement("TextLabel", {
				LayoutOrder = self.props.LayoutOrder,
				Text = "RML: " .. self.props.name .. " failed",
				[React.Tag] = tag,
				BackgroundTransparency = 1,
				AutomaticSize = Enum.AutomaticSize.XY,
				Size = UDim2.fromOffset(0, 0),
			})
		end
		return React.createElement(self.props.component, self.props.inner)
	end

	return function(name, component)
		return function(props)
			return React.createElement(Boundary, { name = name, component = component, inner = props, LayoutOrder = props.LayoutOrder })
		end
	end
end
