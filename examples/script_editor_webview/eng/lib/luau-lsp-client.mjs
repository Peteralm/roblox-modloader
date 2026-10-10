// A luau-lsp process driven over stdio the way the mod drives it: same command line, same settings,
// same notifications in the same order. Checks that run through this see what the editor would see,
// without a Studio in the loop.

import { spawn } from "node:child_process";

export const WORKSPACE_ROOT = "file:///rml";

/** The settings LuauLspBridge.ApplyRobloxSettings sends; the two must say the same thing. */
function settings(definitions, fragmentAutocomplete) {
	return {
		platform: { type: "roblox" },
		sourcemap: { enabled: true, autogenerate: false },
		types: { roblox: true, definitionFiles: { "@roblox": definitions } },
		completion: { enableFragmentAutocomplete: fragmentAutocomplete },
	};
}

export async function startServer({ executable, definitions, fragmentAutocomplete = false }) {
	const server = spawn(executable, ["lsp", "--stdio", "--definitions", `@roblox=${definitions}`], {
		stdio: ["pipe", "pipe", "ignore"],
	});

	const pending = new Map();
	const messages = [];
	const versions = new Map();
	let buffer = Buffer.alloc(0);
	let nextId = 1;

	const send = (message) => {
		const payload = Buffer.from(JSON.stringify(message));
		server.stdin.write(`Content-Length: ${payload.length}\r\n\r\n`);
		server.stdin.write(payload);
	};

	server.stdout.on("data", (chunk) => {
		buffer = Buffer.concat([buffer, chunk]);

		for (;;) {
			const headerEnd = buffer.indexOf("\r\n\r\n");
			if (headerEnd < 0) return;

			const length = Number(/Content-Length: (\d+)/i.exec(buffer.subarray(0, headerEnd).toString())[1]);
			if (buffer.length < headerEnd + 4 + length) return;

			const message = JSON.parse(buffer.subarray(headerEnd + 4, headerEnd + 4 + length).toString());
			buffer = buffer.subarray(headerEnd + 4 + length);

			if (message.id !== undefined && message.method) send({ jsonrpc: "2.0", id: message.id, result: null });
			else if (message.id !== undefined) pending.get(message.id)?.(message);
			else messages.push(message);
		}
	});

	const exited = new Promise((resolve) => server.once("exit", resolve));

	const request = (method, params) => new Promise((resolve, reject) => {
		const id = nextId++;
		const timer = setTimeout(() => reject(new Error(`luau-lsp did not answer '${method}' in 30s`)), 30_000);

		pending.set(id, (message) => {
			clearTimeout(timer);
			pending.delete(id);
			if (message.error) reject(new Error(`${method}: ${message.error.message}`));
			else resolve(message.result);
		});

		send({ jsonrpc: "2.0", id, method, params });
	});

	const notify = (method, params) => send({ jsonrpc: "2.0", method, params });

	await request("initialize", {
		processId: null,
		rootUri: WORKSPACE_ROOT,
		workspaceFolders: [{ uri: WORKSPACE_ROOT, name: "rml" }],
		initializationOptions: settings(definitions, fragmentAutocomplete),
		capabilities: {
			workspace: { configuration: false, workspaceFolders: true },
			textDocument: { completion: { completionItem: { snippetSupport: true } } },
		},
	});

	notify("initialized", {});
	notify("workspace/didChangeConfiguration", { settings: settings(definitions, fragmentAutocomplete) });

	return {
		/** Everything the server said that was not an answer: log lines, diagnostics, warnings. */
		messages,

		/** Opens the document, or replaces its text when it is already open. */
		setText(uri, text) {
			const version = (versions.get(uri) ?? 0) + 1;
			versions.set(uri, version);

			if (version === 1) {
				notify("textDocument/didOpen", { textDocument: { uri, languageId: "luau", version, text } });
			} else {
				notify("textDocument/didChange", { textDocument: { uri, version }, contentChanges: [{ text }] });
			}
		},

		/** The DataModel tree, as the companion plugin protocol carries it. */
		pushDataModel(tree) {
			notify("$/plugin/full", tree);
		},

		/**
		 * Completion items at the end of `text`, which becomes the document's content: the editor's
		 * own sequence, a change of the whole text followed by the request.
		 */
		async completeAtEnd(uri, text) {
			this.setText(uri, text);

			const lines = text.split("\n");
			const result = await request("textDocument/completion", {
				textDocument: { uri },
				position: { line: lines.length - 1, character: lines.at(-1).length },
				context: { triggerKind: 1 },
			});

			return Array.isArray(result) ? result : result?.items ?? [];
		},

		diagnostics(uri) {
			return messages.findLast((message) =>
				message.method === "textDocument/publishDiagnostics" && message.params.uri === uri)?.params.diagnostics ?? [];
		},

		async stop() {
			server.kill();
			await exited;
		},
	};
}
