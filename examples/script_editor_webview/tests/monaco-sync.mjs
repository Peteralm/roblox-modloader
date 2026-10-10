// Regression check for the Monaco overlay, meant to be the first thing run after a Studio update.
//
// It launches a Studio that has this mod installed, waits for the overlay to attach to the scratch
// document the mod opens for us, types into Monaco, and compares the editor's buffer with the text
// the engine actually holds. A Studio build that broke the write path fails here in under a minute
// instead of silently eating keystrokes in someone's session.
//
//   node tests/monaco-sync.mjs --studio C:/b/studio-monaco
//
// Options: --studio <dir> (required)   --port <cdp port, default 9223>
//          --chars <how many characters to type, default 400>   --keep (leave Studio running)
//          --place <rbxlx/rbxl to open, default a blank place this script writes>

import { launchStudio, sleep, waitFor } from "./lib/studio-harness.mjs";

const args = process.argv.slice(2);
const option = (name, fallback) => {
	const at = args.indexOf(`--${name}`);
	return at === -1 ? fallback : args[at + 1];
};

const studioDir = option("studio");
const port = Number(option("port", "9223"));
const charCount = Number(option("chars", "400"));
const keepStudio = args.includes("--keep");

if (!studioDir) {
	console.error("usage: node tests/monaco-sync.mjs --studio <studio directory> [--port 9223] [--chars 400]");
	process.exit(2);
}

let studio;

async function run() {
	studio = await launchStudio({ studioDir, port, place: option("place") });

	await waitFor("the engine agent", () => studio.runLog().includes("engine agent running"), 180_000);
	console.log("engine agent running");

	await waitFor("the overlay to attach", () => studio.runLog().includes("attached editor overlay"), 120_000);
	console.log("overlay attached");

	// Studio can hold more than one overlay page at a time; the one to type into is the plain
	// scratch script, whose tab is in front, not the worker script the mod opens beside it.
	const page = await studio.pageFor("the scratch script", (text) => text.startsWith("-- rml monaco self test\n"));
	console.log("monaco ready");

	const body = "local total = 0\nfor index = 1, 10 do\n\ttotal += index * index\nend\nprint(total)\n";
	const typed = body.repeat(Math.max(1, Math.ceil(charCount / body.length)));
	const remoteBefore = await page.evaluate("window.__rmlRemoteChanges()");

	// One key every 40 ms, like a fast typist: acknowledgements and text reports then land between
	// keystrokes, which is where a stale report used to overwrite the buffer and move the cursor.
	await page.evaluate(`(async () => {
		const editor = monaco.editor.getEditors()[0];
		editor.focus();
		const model = editor.getModel();
		editor.setPosition({ lineNumber: model.getLineCount(), column: 1 });
		for (const character of ${JSON.stringify(typed)}) {
			editor.trigger('kb', 'type', { text: character });
			await new Promise((resolve) => setTimeout(resolve, 40));
		}
		return true;
	})()`);

	console.log(`typed ${typed.length} characters`);

	await waitFor("the engine to catch up", async () => await page.evaluate("window.__rmlPending()") === 0, 60_000);

	await page.evaluate("window.__rmlVerify()");
	const engineText = await waitFor("the engine's copy of the document",
		() => page.evaluate("window.__rmlEngineText"), 30_000);
	const editorText = await page.evaluate("window.__rmlModelText()");

	if (engineText !== editorText) {
		const at = [...editorText].findIndex((character, index) => engineText[index] !== character);

		if (at === -1) {
			throw new Error("the engine and the editor agree on every character they share but not on the length: " +
				`editor has ${editorText.length}, engine has ${engineText.length}`);
		}
		throw new Error(`the engine and the editor disagree at character ${at}: ` +
			`editor has ${JSON.stringify(editorText.slice(at, at + 40))}, ` +
			`engine has ${JSON.stringify(engineText.slice(at, at + 40))}`);
	}

	// The engine only echoed our own typing, so no report should have rewritten the buffer. One that
	// did is a stale copy winning a race, and it drags the user's cursor with it.
	const remoteChanges = await page.evaluate("window.__rmlRemoteChanges()") - remoteBefore;
	if (remoteChanges !== 0) {
		throw new Error(`the engine rewrote the editor ${remoteChanges} time(s) while it only echoed our own typing`);
	}

	// The other half of the mod: luau-lsp answering with types from the sourcemap the agent built.
	await page.evaluate(`(() => {
		const editor = monaco.editor.getEditors()[0];
		editor.focus();
		const model = editor.getModel();
		editor.setPosition({ lineNumber: model.getLineCount(), column: 1 });
		editor.trigger('kb', 'type', { text: 'workspace:Find' });
		return true;
	})()`);

	await sleep(2500);
	await page.evaluate("monaco.editor.getEditors()[0].trigger('kb','editor.action.triggerSuggest',{})");

	const suggestions = await waitFor("luau-lsp completions", async () => {
		const rows = await page.evaluate("(() => { const w = document.querySelector('.suggest-widget.visible'); return w ? [...w.querySelectorAll('.monaco-list-row')].map(r => r.textContent) : []; })()");
		return rows.length > 0 ? rows : undefined;
	}, 30_000);

	if (!suggestions.some((row) => row.includes("FindFirstChild"))) {
		throw new Error(`luau-lsp answered without the Instance API: ${JSON.stringify(suggestions.slice(0, 5))}`);
	}

	console.log(`completions: ${suggestions.length} entries, typed from the sourcemap`);

	// Other mods installed in the same Studio log their own errors; only this mod's fail the check.
	const errors = studio.errors().filter((line) => /ScriptEditorWebview|luau-lsp|WebView2|engine agent/i.test(line));
	if (errors.length > 0) throw new Error(`the mod logged ${errors.length} error(s):\n${errors.join("\n")}`);

	const dumps = studio.newCrashDumps();
	if (dumps.length > 0) throw new Error(`new crash dump(s): ${dumps.join(", ")}`);

	page.close();

	console.log(`in sync: ${editorText.length} characters, no errors logged`);
}

try {
	await run();
	console.log("PASS");
} catch (failure) {
	console.error(`FAIL: ${failure.message}`);
	process.exitCode = 1;
} finally {
	if (studio && !keepStudio) await studio.stop();
}
