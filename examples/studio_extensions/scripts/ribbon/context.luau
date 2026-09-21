--!nocheck

return {
	log = require("@self/lib/log")("ribbon"),
	session = nil,
	m = nil,
	state = {
		reactHooked = false,
		lastDefinition = nil,
		dump = nil,
		reload = nil,
		onReactHook = nil,
	},
}
