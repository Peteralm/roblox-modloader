--!nocheck

local Session = {}
Session.__index = Session

local function attach(anchor, key, log)
	local previous = rawget(anchor, key)
	if previous then
		previous:dispose()
		log.info("cleaned up the previous session (%d connections, %d hooks)", #previous.connections, previous.count)
	end
	local session = setmetatable({
		connections = {},
		hooks = {},
		restores = {},
		count = 0,
		stats = {},
	}, Session)
	rawset(anchor, key, session)
	return session
end

function Session:track(conn)
	table.insert(self.connections, conn)
	return conn
end

function Session:hook(target, replacement)
	pcall(restorefunction, target)
	local original = hookfunction(target, replacement)
	self.hooks[target] = true
	self.count += 1
	return original
end

function Session:unhook(target)
	if self.hooks[target] then
		pcall(restorefunction, target)
		self.hooks[target] = nil
		self.count -= 1
	end
end

function Session:onCleanup(fn)
	table.insert(self.restores, fn)
end

function Session:dispose()
	for _, conn in ipairs(self.connections) do
		pcall(function() conn:Disconnect() end)
	end
	for target in pairs(self.hooks) do
		pcall(restorefunction, target)
	end
	for _, restore in ipairs(self.restores) do
		pcall(restore)
	end
end

return { attach = attach }
