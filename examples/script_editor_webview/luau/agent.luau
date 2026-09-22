--!nocheck
-- Engine-side half of the Monaco overlay.
--
-- Everything this mod does to a script document happens here, in Luau, through the documented
-- ScriptEditorService API: opening and closing documents, reading their text, writing the editor's
-- edits back, and walking the DataModel for luau-lsp's sourcemap. The .NET half owns the WebView,
-- the Qt window and the language server, and talks to this table through two calls that never
-- yield -- `push` to queue work, `poll` to drain what happened -- so it never has to reach into
-- engine memory itself. That is what makes the mod survive a Studio update: the contract it stands
-- on is the public API, not a struct layout.

local HttpService = game:GetService("HttpService")
local ScriptEditorService = game:GetService("ScriptEditorService")

-- The walk is cooperative: it hands the engine a frame back every so many instances so a place with
-- 100k parts cannot freeze the editor while the sourcemap is built.
local SOURCEMAP_NODE_BUDGET = 2000
local SOURCEMAP_DEBOUNCE_SECONDS = 1

local agent = {}

agent._documents = {}
agent._idByDocument = {}
agent._events = {}
agent._nextId = 0
agent._connections = {}
agent._running = true

agent._sourcemapJson = nil
agent._sourcemapNodes = 0
agent._sourcemapWalking = false
agent._sourcemapDebounceToken = 0

function agent._emit(event)
	table.insert(agent._events, event)
end

function agent._describe(document)
	local ok, script = pcall(function()
		return document:GetScript()
	end)

	if not ok or not script then
		return nil
	end

	return script:GetFullName()
end

function agent._textOf(document)
	local ok, text = pcall(function()
		return document:GetText()
	end)

	return ok and text or ""
end

function agent._track(document)
	local existing = agent._idByDocument[document]
	if existing then
		return existing
	end

	-- The command bar is a ScriptDocument too, and it is the one document an overlay must never
	-- take over. A document we cannot classify is left to Studio.
	local ok, isCommandBar = pcall(function()
		return document:IsCommandBar()
	end)
	if not ok or isCommandBar then
		return nil
	end

	local name = agent._describe(document)
	if not name then
		return nil
	end

	agent._nextId += 1
	local id = agent._nextId

	local entry = {
		id = id,
		document = document,
		name = name,
		queue = {},
		applied = 0,
		pumping = false,
		closed = false,
	}

	agent._documents[id] = entry
	agent._idByDocument[document] = id

	local uri = ""
	local gotUri, value = pcall(function()
		return document:GetInternalUri()
	end)
	if gotUri then
		uri = value
	end

	agent._emit({
		kind = "open",
		id = id,
		name = name,
		uri = uri,
		text = agent._textOf(document),
	})

	return id
end

function agent._untrack(document)
	local id = agent._idByDocument[document]
	if not id then
		return
	end

	local entry = agent._documents[id]
	if entry then
		entry.closed = true
		entry.queue = {}
	end

	agent._documents[id] = nil
	agent._idByDocument[document] = nil

	agent._emit({ kind = "close", id = id })
end

-- One edit at a time, newest range first. Every range the editor sends is expressed against the
-- document as the editor left it, so applying a batch bottom-up keeps the ranges that are still
-- pending valid: an edit low in the file cannot move the lines above it.
function agent._pump(entry)
	if entry.pumping then
		return
	end

	entry.pumping = true

	task.spawn(function()
		while agent._running and not entry.closed and #entry.queue > 0 do
			local edit = table.remove(entry.queue, 1)

			local ok, err = pcall(function()
				return entry.document:EditTextAsync(
					edit.text,
					edit.startLine,
					edit.startCharacter,
					edit.endLine,
					edit.endCharacter
				)
			end)

			entry.applied += 1

			if not ok then
				agent._emit({
					kind = "error",
					id = entry.id,
					message = `writing an edit failed: {err}`,
				})
			end
		end

		entry.pumping = false

		if agent._running and not entry.closed then
			-- Every change reported while the queue was draining was marked stale, so this is the
			-- one report the editor can trust: the document after the last write landed.
			agent._emit({
				kind = "text",
				id = entry.id,
				text = agent._textOf(entry.document),
				stale = false,
			})
		end
	end)
end

-- Called by the .NET half for every batch of keystrokes. Returns immediately: the write itself is
-- a yielding call and lives in the pump coroutine, where the engine owns the resume.
function agent.push(payload)
	if not agent._running then
		return false
	end

	local ok, request = pcall(function()
		return HttpService:JSONDecode(payload)
	end)
	if not ok or type(request) ~= "table" then
		return false
	end

	local entry = agent._documents[request.id]
	if not entry or entry.closed then
		return false
	end

	local batch = request.edits
	if type(batch) ~= "table" or #batch == 0 then
		return false
	end

	table.sort(batch, function(left, right)
		if left.startLine ~= right.startLine then
			return left.startLine > right.startLine
		end

		return left.startCharacter > right.startCharacter
	end)

	for _, edit in batch do
		table.insert(entry.queue, edit)
	end

	agent._pump(entry)
	return true
end

-- Re-announces every document that is still open, for the Mods menu's reattach: the .NET half
-- drops its overlays and needs the open events again to build new ones.
function agent.resync()
	local announced = 0

	for id, entry in agent._documents do
		local uri = ""
		local gotUri, value = pcall(function()
			return entry.document:GetInternalUri()
		end)
		if gotUri then
			uri = value
		end

		agent._emit({
			kind = "open",
			id = id,
			name = entry.name,
			uri = uri,
			text = agent._textOf(entry.document),
		})

		announced += 1
	end

	agent._markSourcemapDirty()

	return announced
end

-- Drains everything that happened since the last call: documents opened and closed, text the engine
-- changed under us, how many of our own writes landed, and the sourcemap once a walk finishes.
function agent.poll()
	local events = agent._events
	agent._events = {}

	local applied = {}
	for id, entry in agent._documents do
		if entry.applied > 0 then
			table.insert(applied, { id = id, count = entry.applied })
			entry.applied = 0
		end
	end

	local payload = {
		events = events,
		applied = applied,
	}

	if agent._sourcemapJson then
		payload.sourcemap = agent._sourcemapJson
		payload.sourcemapNodes = agent._sourcemapNodes
		agent._sourcemapJson = nil
	end

	return HttpService:JSONEncode(payload)
end

-- The overlay only goes up once a real write has been proven on this Studio build: a no-op edit at
-- the very start of the document, which must succeed and must leave the text alone. A build that
-- broke the API fails here, visibly, instead of a WebView that silently swallows what you type.
--
-- The answer comes back as an event rather than a return value: the write yields, and a call the
-- .NET half made returns the moment the thread parks, so anything a yield produces has to be
-- reported on the next poll.
function agent.check(id)
	local entry = agent._documents[id]
	if not entry then
		agent._emit({ kind = "checked", id = id, ok = false, message = "the document is no longer open" })
		return false
	end

	task.spawn(function()
		local before = agent._textOf(entry.document)

		local ok, err = pcall(function()
			return entry.document:EditTextAsync("", 1, 1, 1, 1)
		end)

		if not ok then
			agent._emit({ kind = "checked", id = id, ok = false, message = tostring(err) })
			return
		end

		local after = agent._textOf(entry.document)
		if after ~= before then
			agent._emit({
				kind = "checked",
				id = id,
				ok = false,
				message = "a no-op edit changed the document text",
			})
			return
		end

		agent._emit({ kind = "checked", id = id, ok = true, text = after })
	end)

	return true
end

-- The text the engine holds for one document. Reading is not a yielding call, so unlike a write
-- this one can answer inline -- which is what lets a regression check compare the two buffers.
function agent.text(id)
	local entry = agent._documents[id]
	if not entry then
		return ""
	end

	return agent._textOf(entry.document)
end

-- Opens a scratch script so an automated check has a document to type into without a human
-- clicking one open. Only ever called when the mod is started with RML_MONACO_SELFTEST set.
function agent.openScratch()
	task.spawn(function()
		local script = Instance.new("Script")
		script.Name = "RmlMonacoSelfTest"
		script.Source = "-- rml monaco self test\n"
		script.Parent = game:GetService("ServerScriptService")

		local ok, err = pcall(function()
			return ScriptEditorService:OpenScriptDocumentAsync(script)
		end)

		if not ok then
			agent._emit({ kind = "error", message = `opening the scratch document failed: {err}` })
		end
	end)

	return true
end

function agent._markSourcemapDirty()
	agent._sourcemapDebounceToken += 1

	local token = agent._sourcemapDebounceToken

	task.delay(SOURCEMAP_DEBOUNCE_SECONDS, function()
		if not agent._running or token ~= agent._sourcemapDebounceToken then
			return
		end

		agent._walkSourcemap()
	end)
end

-- luau-lsp reads the place as a Rojo sourcemap: a tree of Name/ClassName/Children. Building it here
-- costs one engine call per instance instead of one interop round trip per instance.
function agent._walkSourcemap()
	if agent._sourcemapWalking or not agent._running then
		return
	end

	agent._sourcemapWalking = true

	task.spawn(function()
		local visited = 0

		local function node(instance, name, className)
			local mapped = {
				Name = name,
				ClassName = className,
			}

			local children = {}

			for _, child in instance:GetChildren() do
				visited += 1
				if visited % SOURCEMAP_NODE_BUDGET == 0 then
					task.wait()

					if not agent._running then
						error("the data model went away", 0)
					end
				end

				table.insert(children, node(child, child.Name, child.ClassName))
			end

			if #children > 0 then
				mapped.Children = children
			end

			return mapped
		end

		local walked, tree = pcall(function()
			return node(game, "game", "DataModel")
		end)

		if walked then
			local encoded, json = pcall(function()
				return HttpService:JSONEncode(tree)
			end)

			if encoded then
				agent._sourcemapJson = json
				agent._sourcemapNodes = visited + 1
			else
				agent._emit({ kind = "error", message = `encoding the sourcemap failed: {json}` })
			end
		end

		agent._sourcemapWalking = false
	end)
end

function agent.stop()
	agent._running = false

	for _, connection in agent._connections do
		pcall(function()
			connection:Disconnect()
		end)
	end

	agent._connections = {}
	agent._documents = {}
	agent._idByDocument = {}
	agent._events = {}
end

function agent._start()
	table.insert(
		agent._connections,
		ScriptEditorService.TextDocumentDidOpen:Connect(function(document)
			agent._track(document)
		end)
	)

	table.insert(
		agent._connections,
		ScriptEditorService.TextDocumentDidClose:Connect(function(document)
			agent._untrack(document)
		end)
	)

	table.insert(
		agent._connections,
		ScriptEditorService.TextDocumentDidChange:Connect(function(document)
			local id = agent._idByDocument[document]
			if not id then
				return
			end

			local entry = agent._documents[id]
			if not entry then
				return
			end

			-- A change reported while our own writes are still in flight describes a document the
			-- editor has already typed past; pushing it back would undo those keystrokes.
			agent._emit({
				kind = "text",
				id = id,
				text = agent._textOf(document),
				stale = entry.pumping or #entry.queue > 0,
			})
		end)
	)

	table.insert(agent._connections, game.DescendantAdded:Connect(function()
		agent._markSourcemapDirty()
	end))

	table.insert(agent._connections, game.DescendantRemoving:Connect(function()
		agent._markSourcemapDirty()
	end))

	for _, document in ScriptEditorService:GetScriptDocuments() do
		agent._track(document)
	end

	agent._markSourcemapDirty()
end

agent._start()

return agent
