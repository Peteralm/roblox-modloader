// What every in-Studio check needs: start a Studio that has the mod installed, read what the
// loader logged since, reach the editor pages over the WebView's debugging port, and talk to the
// mod when no page is up (its self-test channel).

import { spawn, spawnSync } from "node:child_process";
import {
	closeSync, existsSync, mkdtempSync, openSync, readdirSync, readFileSync, readSync, renameSync, rmSync, statSync,
	writeFileSync,
} from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";

export const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

export async function waitFor(what, predicate, timeoutMs) {
	const deadline = Date.now() + timeoutMs;

	while (Date.now() < deadline) {
		const found = await predicate();
		if (found) return found;

		await sleep(250);
	}

	throw new Error(`timed out waiting for ${what} after ${timeoutMs / 1000}s`);
}

/** Minimal CDP client: one socket, Runtime.evaluate, nothing else. */
async function connect(webSocketUrl) {
	const socket = new WebSocket(webSocketUrl);
	const pending = new Map();
	let nextId = 1;

	socket.addEventListener("message", (event) => {
		const message = JSON.parse(event.data);
		const settle = pending.get(message.id);
		if (!settle) return;

		pending.delete(message.id);
		settle(message);
	});

	await new Promise((resolve, reject) => {
		socket.addEventListener("open", resolve, { once: true });
		socket.addEventListener("error", reject, { once: true });
	});

	return {
		async evaluate(expression) {
			const id = nextId++;
			const answer = new Promise((resolve) => pending.set(id, resolve));

			socket.send(JSON.stringify({
				id,
				method: "Runtime.evaluate",
				params: { expression, returnByValue: true, awaitPromise: true },
			}));

			const message = await answer;
			if (message.error) throw new Error(message.error.message);
			if (message.result?.exceptionDetails) {
				throw new Error(message.result.exceptionDetails.exception?.description ?? message.result.exceptionDetails.text);
			}

			return message.result.result.value;
		},
		close() {
			socket.close();
		},
	};
}

function processes(filter) {
	const run = spawnSync("powershell", ["-NoProfile", "-Command",
		`Get-CimInstance Win32_Process -Filter "${filter}" | Select-Object ProcessId, ParentProcessId, Name, CommandLine | ConvertTo-Json -Compress`],
	{ encoding: "utf8" });

	const text = (run.stdout ?? "").trim();
	if (!text) return [];

	const parsed = JSON.parse(text);
	return Array.isArray(parsed) ? parsed : [parsed];
}

/**
 * Starts the Studio in `studioDir` on a blank place with the mod's self-test switched on.
 * The WebView gets a user-data folder of its own: with the mod's usual one, this Studio would share
 * a browser process with any other Studio running the mod, and the debugging port asked for here
 * would be refused or, worse, answered by that other Studio's pages.
 */
export async function launchStudio({ studioDir, port, place }) {
	const executable = join(studioDir, "RobloxStudioBeta.exe");
	const logDirectory = join(studioDir, "RobloxModLoader", "logs");
	const crashDirectory = join(studioDir, "RobloxModLoader", "crashes");
	const modDirectory = join(studioDir, "RobloxModLoader", "mods", "script-editor-webview");

	if (!existsSync(executable)) throw new Error(`no Studio at '${executable}'`);
	if (!existsSync(modDirectory)) throw new Error(`the mod is not installed at '${modDirectory}'`);

	const answering = async () => {
		try {
			await fetch(`http://127.0.0.1:${port}/json/version`);
			return true;
		} catch {
			return false;
		}
	};

	// WebView2 takes the debugging port from the environment of the Studio that owns it: if
	// something already holds this port, every page we would see belongs to that instead.
	if (await answering()) throw new Error(`something is already serving CDP on port ${port}; pass --port`);

	const logFiles = () => (existsSync(logDirectory)
		? readdirSync(logDirectory).filter((name) => name.endsWith(".log")).sort()
		: []);

	// The log is append-only and a Studio started earlier may still own the file, so the run reads
	// only what is written after this point instead of clearing it. Only that tail is ever read:
	// a test Studio's logs grow past what fits in one string.
	const baseline = new Map(logFiles().map((name) => [name, statSync(join(logDirectory, name)).size]));

	const logText = () => logFiles().map((name) => {
		const file = join(logDirectory, name);
		const from = baseline.get(name) ?? 0;
		const length = statSync(file).size - from;
		if (length <= 0) return "";

		const descriptor = openSync(file, "r");
		try {
			const tail = Buffer.alloc(length);
			return tail.subarray(0, readSync(descriptor, tail, 0, length, from)).toString("utf8");
		} finally {
			closeSync(descriptor);
		}
	}).join("\n");

	const crashDumps = () => (existsSync(crashDirectory) ? readdirSync(crashDirectory).sort() : []);
	const dumpsBefore = crashDumps();

	const scratch = mkdtempSync(join(tmpdir(), "rml-monaco-"));
	if (!place) {
		// Studio started with no place shows the start page and never loads an Edit data model, so
		// the mod would have nothing to attach to. The smallest place Studio accepts is this one
		// line of XML; it opens with the default services and nothing else.
		place = join(scratch, "blank.rbxlx");
		writeFileSync(place, '<roblox version="4"></roblox>\n');
	}

	const userData = join(scratch, "webview2");
	const studio = spawn(executable, ["-task", "EditFile", "-localPlaceFile", place], {
		cwd: studioDir,
		detached: false,
		stdio: "ignore",
		env: {
			...process.env,
			RML_MONACO_SELFTEST: "1",
			WEBVIEW2_USER_DATA_FOLDER: userData,
			WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS: `--remote-debugging-port=${port}`,
		},
	});

	const exited = new Promise((resolve) => studio.once("exit", resolve));
	let commandId = 0;

	const harness = {
		pid: studio.pid,
		modDirectory,
		settingsFile: join(modDirectory, "script-editor-webview.toml"),

		/** Everything the loader logged since this Studio started. */
		runLog: logText,

		count(needle) {
			return harness.runLog().split(needle).length - 1;
		},

		errors() {
			return harness.runLog().split("\n").filter((line) => line.includes("[error]") || line.includes("[critical]"));
		},

		newCrashDumps() {
			return crashDumps().filter((name) => !dumpsBefore.includes(name));
		},

		/** The editor pages that finished loading, each with the text of its document. */
		async pages() {
			let targets;
			try {
				targets = await (await fetch(`http://127.0.0.1:${port}/json/list`)).json();
			} catch {
				return [];
			}

			const ready = [];

			for (const target of targets.filter((candidate) => candidate.url.includes("rml.scripteditor"))) {
				let page;
				try {
					page = await connect(target.webSocketDebuggerUrl);
					if (await page.evaluate("typeof window.__rmlSynced === 'function' && window.__rmlSynced()")) {
						ready.push({ page, text: await page.evaluate("window.__rmlModelText()") });
						continue;
					}
				} catch {
					// A page that is still navigating rejects the evaluate; it is simply not ready.
				}

				page?.close();
			}

			return ready;
		},

		/** How many editor pages the WebView holds, loaded or not. */
		async pageCount() {
			try {
				const targets = await (await fetch(`http://127.0.0.1:${port}/json/list`)).json();
				return targets.filter((candidate) => candidate.url.includes("rml.scripteditor")).length;
			} catch {
				// Nothing listens on the port any more: the browser process is gone, and its pages with it.
				return 0;
			}
		},

		/** The page showing the document whose text satisfies `matches`; the others are closed. */
		async pageFor(what, matches, timeoutMs = 120_000) {
			return waitFor(`the editor page of ${what}`, async () => {
				let found;

				for (const candidate of await harness.pages()) {
					if (!found && matches(candidate.text)) found = candidate.page;
					else candidate.page.close();
				}

				return found;
			}, timeoutMs);
		},

		/** Browser processes of this Studio's WebView, and language servers it started. */
		helperProcesses() {
			const browsers = processes("Name='msedgewebview2.exe'")
				.filter((entry) => (entry.CommandLine ?? "").toLowerCase().includes(userData.toLowerCase()));
			const servers = processes("Name='luau-lsp.exe'").filter((entry) => entry.ParentProcessId === studio.pid);

			return { browsers: browsers.length, servers: servers.length };
		},

		/** Sends one command through the mod's self-test channel and returns its answer. */
		async selfTest(command) {
			const id = ++commandId;
			const commandFile = join(modDirectory, "selftest-command.json");
			const resultFile = join(modDirectory, "selftest-result.json");

			rmSync(resultFile, { force: true });
			writeFileSync(`${commandFile}.tmp`, JSON.stringify({ id, ...command }));
			renameSync(`${commandFile}.tmp`, commandFile);

			const answer = await waitFor(`the mod to answer self-test command '${command.action}'`, () => {
				try {
					const parsed = JSON.parse(readFileSync(resultFile, "utf8"));
					return parsed.id === id ? parsed : undefined;
				} catch {
					return undefined;
				}
			}, 30_000);

			if (!answer.ok) throw new Error(`self-test command '${command.action}' failed: ${answer.value}`);

			return answer.value;
		},

		/** Runs Luau in the Edit data model. It must not yield: what it returns is the answer. */
		eval(source) {
			return harness.selfTest({ action: "eval", source });
		},

		async stop() {
			// Killing Studio does not take the processes it started with it. Only the ones that are
			// provably this Studio's are touched: its own language servers, and browser processes
			// running on the user-data folder made for this run.
			const servers = processes("Name='luau-lsp.exe'").filter((entry) => entry.ParentProcessId === studio.pid);

			studio.kill();
			await Promise.race([exited, sleep(15_000)]);
			await sleep(1500);

			const browsers = processes("Name='msedgewebview2.exe'")
				.filter((entry) => (entry.CommandLine ?? "").toLowerCase().includes(userData.toLowerCase()));

			for (const entry of [...servers, ...browsers]) {
				spawnSync("taskkill", ["/PID", String(entry.ProcessId), "/F"], { stdio: "ignore" });
			}

			rmSync(join(modDirectory, "selftest-command.json"), { force: true });
			rmSync(join(modDirectory, "selftest-result.json"), { force: true });

			// The browser processes outlive a killed Studio for a moment and hold the folder open.
			for (let attempt = 0; attempt < 20; attempt++) {
				try {
					rmSync(scratch, { recursive: true, force: true });
					break;
				} catch {
					await sleep(500);
				}
			}
		},
	};

	console.log(`studio ${studio.pid} starting`);

	return harness;
}
