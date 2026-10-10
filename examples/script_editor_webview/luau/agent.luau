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
local StudioService = game:GetService("StudioService")

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

-- Script instance -> the path luau-lsp knows it by, for the documents that are open. The language
-- server ties a document to its place in the DataModel only through a file path, so every open
-- script gets one made of its document id; nothing is ever written there.
agent._pathByScript = {}

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
		script = document:GetScript(),
		name = name,
		path = `scripts/{id}.luau`,
		queue = {},
		applied = 0,
		pumping = false,
		closed = false,
		-- Set once the editor has handed this document back to Studio's own editor: the text it
		-- sent last is the truth from then on, and an edit that arrives late must not land.
		frozen = false,
		-- Counts the hand-backs asked for, so one that was overtaken can tell and stand down.
		settling = 0,
	}

	agent._documents[id] = entry
	agent._idByDocument[document] = id
	agent._pathByScript[entry.script] = entry.path

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
		path = entry.path,
		text = agent._textOf(document),
	})

	-- The tree luau-lsp holds has no path for this script yet, so it cannot say what `script` is.
	agent._markSourcemapDirty()

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
		agent._pathByScript[entry.script] = nil
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
	if not entry or entry.closed or entry.frozen then
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
		-- Whoever asks for the documents again is about to edit them again.
		entry.frozen = false
		entry.settling += 1

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
			path = entry.path,
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
		active = agent._activeId(),
		activeKnown = agent._activeKnown,
	}

	if agent._sourcemapJson then
		payload.sourcemap = agent._sourcemapJson
		payload.sourcemapNodes = agent._sourcemapNodes
		agent._sourcemapJson = nil
	end

	return HttpService:JSONEncode(payload)
end

-- The document whose tab is in front, or 0 when none is. Only that document's editor window is
-- visible, so it is the only one an overlay can be paired with by looking at windows.
-- `_activeKnown` turns false when Studio refuses to say, and the 0 then means nothing.
function agent._activeId()
	local ok, script = pcall(function()
		return StudioService.ActiveScript
	end)

	agent._activeKnown = ok

	if not ok or not script then
		return 0
	end

	for id, entry in agent._documents do
		if entry.script == script then
			return id
		end
	end

	return 0
end

-- Hands one document back to Studio's own editor. The editor is frozen by the time it calls this
-- and `text` is everything it holds; the document must hold exactly that before the overlay goes
-- away. Writes still queued are let through first, and if the two still differ the whole document
-- is rewritten once. The answer is a "settled" event, for the reason `check` answers with one.
function agent.settle(payload)
	local decoded, request = pcall(function()
		return HttpService:JSONDecode(payload)
	end)
	if not decoded or type(request) ~= "table" or type(request.text) ~= "string" then
		return false
	end

	local id = request.id
	local entry = agent._documents[id]
	if not entry or entry.closed then
		-- A document that closed in the meantime took its text with it: nothing to hand over.
		agent._emit({ kind = "settled", id = id, ok = true, length = 0, rewritten = false })
		return true
	end

	entry.frozen = true
	entry.settling += 1

	local turn = entry.settling

	task.spawn(function()
		while agent._running and not entry.closed and (entry.pumping or #entry.queue > 0) do
			task.wait()
		end

		if not agent._running or entry.closed then
			agent._emit({ kind = "settled", id = id, ok = true, length = 0, rewritten = false })
			return
		end

		-- The editor was given back to the user while this waited (`resync`), or it asked again:
		-- the text this call was handed is no longer the editor's last word.
		if entry.settling ~= turn then
			return
		end

		local current = agent._textOf(entry.document)
		local rewritten = false

		if current ~= request.text then
			rewritten = true

			local lines = string.split(current, "\n")
			local ok, err = pcall(function()
				return entry.document:EditTextAsync(request.text, 1, 1, #lines, #lines[#lines] + 1)
			end)

			if not ok then
				entry.frozen = false
				agent._emit({ kind = "settled", id = id, ok = false, message = `rewriting the document failed: {err}` })
				return
			end

			current = agent._textOf(entry.document)
		end

		if current ~= request.text then
			entry.frozen = false
			agent._emit({
				kind = "settled",
				id = id,
				ok = false,
				message = `the document holds {#current} bytes after the rewrite, the editor holds {#request.text}`,
			})
			return
		end

		agent._emit({ kind = "settled", id = id, ok = true, length = #current, rewritten = rewritten })
	end)

	return true
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

-- Opens two scratch scripts so an automated check has documents to type into without a human
-- clicking them open: a worker inside an Actor, which lets the check ask what `script.Parent` is,
-- and a plain server script, opened last so its tab is the one in front. Only ever called when the
-- mod is started with RML_MONACO_SELFTEST set, and it does nothing the second time: the agent is
-- started again after every switch back from Studio's editor, in the same place.
function agent.openScratch()
	if game:GetService("ServerScriptService"):FindFirstChild("RmlMonacoSelfTest") then
		return false
	end

	task.spawn(function()
		local actor = Instance.new("Actor")
		actor.Name = "RmlMonacoSelfTestActor"
		actor.Parent = workspace

		local worker = Instance.new("Script")
		worker.Name = "RmlMonacoSelfTestWorker"
		worker.Source = "-- rml monaco self test worker\n"
		worker.Parent = actor

		local script = Instance.new("Script")
		script.Name = "RmlMonacoSelfTest"
		script.Source = "-- rml monaco self test\n"
		script.Parent = game:GetService("ServerScriptService")

		for _, target in { worker, script } do
			local ok, err = pcall(function()
				return ScriptEditorService:OpenScriptDocumentAsync(target)
			end)

			if not ok then
				agent._emit({ kind = "error", message = `opening the scratch document failed: {err}` })
			end
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

			local path = agent._pathByScript[instance]
			if path then
				mapped.FilePaths = { path }
			end

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
	agent._pathByScript = {}
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
