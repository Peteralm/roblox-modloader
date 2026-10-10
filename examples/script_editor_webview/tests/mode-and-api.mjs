// Checks, in a real Studio, the two things a Studio update or a change to this mod is most likely
// to break without anyone noticing: that the completion list knows the instance API, and that
// switching between Monaco and Studio's own editor never costs a character.
//
//   node tests/mode-and-api.mjs --studio C:/b/studio-test
//
// It launches that Studio twice (the second time proves the chosen editor survives a restart),
// drives the real editor page through the WebView's debugging port, and reads the engine's side
// through the mod's self-test channel, which works when no editor page exists.
//
// Options: --studio <dir> (required)   --port <cdp port, default 9224>   --trips <round trips, default 10>

import { existsSync, readFileSync, rmSync, writeFileSync } from "node:fs";

import { dumpFromStudio, membersOf } from "../eng/lib/api-dump.mjs";
import { launchStudio, sleep, waitFor } from "./lib/studio-harness.mjs";

const args = process.argv.slice(2);
const option = (name, fallback) => {
	const at = args.indexOf(`--${name}`);
	return at === -1 ? fallback : args[at + 1];
};

const studioDir = option("studio");
const port = Number(option("port", "9224"));
const trips = Number(option("trips", "10"));

if (!studioDir) {
	console.error("usage: node tests/mode-and-api.mjs --studio <studio directory> [--port 9224] [--trips 10]");
	process.exit(2);
}

const MAIN = "-- rml monaco self test\n";
const WORKER = "-- rml monaco self test worker\n";
const isMain = (text) => text.startsWith(MAIN);
const isWorker = (text) => text.startsWith(WORKER);

const ATTACHED = "attached editor overlay";
const NATIVE_IN_CHARGE = "Studio's own editor is in charge";

// The scratch scripts the mod opens for a check, as Luau expressions.
const MAIN_SCRIPT = 'game:GetService("ServerScriptService").RmlMonacoSelfTest';
const WORKER_SCRIPT = "workspace.RmlMonacoSelfTestActor.RmlMonacoSelfTestWorker";

/** Mirrors eng/update-roblox-types.mjs: what the definitions syntax cannot mark as deprecated stays out. */
const expected = (member) => !member.deprecated || member.MemberType === "Function";

function assert(condition, message) {
	if (!condition) throw new Error(message);
}

function firstDifference(left, right) {
	const at = [...left].findIndex((character, index) => right[index] !== character);
	const index = at === -1 ? Math.min(left.length, right.length) : at;

	return `at character ${index}: ${JSON.stringify(left.slice(index, index + 40))} against ${JSON.stringify(right.slice(index, index + 40))}`;
}

/** What the engine holds for both scratch scripts, read without the agent or an editor page. */
async function engineState(studio) {
	return JSON.parse(await studio.eval(`
		local ScriptEditorService = game:GetService("ScriptEditorService")
		local state = {}
		for name, target in { main = ${MAIN_SCRIPT}, worker = ${WORKER_SCRIPT} } do
			local document = ScriptEditorService:FindScriptDocument(target)
			state[name] = { source = target.Source, document = document and document:GetText() or nil }
		end
		return game:GetService("HttpService"):JSONEncode(state)
	`));
}

/** Brings one scratch script's tab to the front, which is what makes its editor window visible. */
function focus(studio, target) {
	return studio.eval(`
		local target = ${target}
		task.spawn(function() game:GetService("ScriptEditorService"):OpenScriptDocumentAsync(target) end)
		return "queued"
	`);
}

/** Types at the end of the document, one key at a time, the way the keyboard would. */
function typeAtEnd(page, text) {
	return page.evaluate(`(async () => {
		const editor = monaco.editor.getEditors()[0];
		const model = editor.getModel();
		editor.setPosition(model.getFullModelRange().getEndPosition());
		for (const character of ${JSON.stringify(text)}) {
			editor.trigger('kb', 'type', { text: character });
			await new Promise((resolve) => setTimeout(resolve, 15));
		}
		return model.getValue();
	})()`);
}

/** Replaces everything after the document's first line and asks for the list at the end of it. */
function completeAfter(page, text) {
	return page.evaluate(`(async () => {
		const editor = monaco.editor.getEditors()[0];
		const model = editor.getModel();
		const end = model.getFullModelRange().getEndPosition();
		editor.executeEdits('rml-check', [{ range: new monaco.Range(2, 1, end.lineNumber, end.column), text: ${JSON.stringify(text)} }]);
		editor.setPosition(model.getFullModelRange().getEndPosition());
		return (await window.__rmlCompletions()).map((item) => ({ label: item.label, deprecated: !!item.tags, sortText: item.sortText }));
	})()`);
}

async function waitForNoPending(page) {
	await waitFor("the engine to catch up", async () => await page.evaluate("window.__rmlPending()") === 0, 60_000);
}

async function checkApi(studio, dump, page) {
	console.log("--- completions");

	// The language server starts with the overlay and reads its definitions first; until it has,
	// a request simply times out in the page.
	const started = Date.now();
	await waitFor("the language server to answer", async () => {
		try {
			return (await completeAfter(page, "workspace:")).length > 0;
		} catch {
			return false;
		}
	}, 120_000);
	console.log(`language server answering ${((Date.now() - started) / 1000).toFixed(1)} s after the overlay came up`);

	// (a) The case that was reported.
	const actor = await completeAfter(page, 'local actor = Instance.new("Actor")\nactor:');
	const actorLabels = new Set(actor.map((item) => item.label));
	for (const name of ["SendMessage", "BindToMessage", "BindToMessageParallel"]) {
		assert(actorLabels.has(name), `Instance.new("Actor"): does not offer ${name}; it offers ${actor.length} items`);
	}
	console.log(`Instance.new("Actor"): -> ${actor.length} items, with SendMessage, BindToMessage, BindToMessageParallel`);

	// The same through the list a person sees: the widget itself, filtered by what was typed.
	await page.evaluate(`(async () => {
		const editor = monaco.editor.getEditors()[0];
		editor.focus();
		editor.trigger('kb', 'type', { text: 'Bind' });
		await new Promise((resolve) => setTimeout(resolve, 300));
		editor.trigger('kb', 'editor.action.triggerSuggest', {});
		return true;
	})()`);
	const rows = await waitFor("the suggestion list", async () => {
		const shown = await page.evaluate("(() => { const widget = document.querySelector('.suggest-widget.visible'); return widget ? [...widget.querySelectorAll('.monaco-list-row')].map((row) => row.getAttribute('aria-label') || row.textContent) : []; })()");
		return shown.length > 0 ? shown : undefined;
	}, 30_000);
	assert(rows.some((row) => row.includes("BindToMessageParallel")), `the suggestion list shows ${JSON.stringify(rows.slice(0, 6))}`);
	console.log(`the suggestion list after typing "actor:Bind" shows: ${rows.map((row) => row.split(",")[0]).join(", ")}`);
	await page.evaluate("monaco.editor.getEditors()[0].trigger('kb', 'hideSuggestWidget', {})");

	// Deprecated members are offered, struck through, after the current ones.
	const model = await completeAfter(page, 'local model = Instance.new("Model")\nmodel:');
	const old = model.find((item) => item.label === "breakJoints");
	const current = model.find((item) => item.label === "GetPivot");
	assert(old?.deprecated && current && !current.deprecated && old.sortText > current.sortText,
		`a deprecated method is not set apart: ${JSON.stringify({ old, current })}`);
	console.log(`deprecated Model:breakJoints is offered struck through and sorted after Model:GetPivot`);

	// (b) A sample across the hierarchy, through the forms a script uses to reach an instance.
	const newOf = (name) => `local value = Instance.new("${name}")\nvalue.`;
	const serviceOf = (name) => `local value = game:GetService("${name}")\nvalue.`;
	const childOf = (name) => `local value = workspace:FindFirstChildOfClass("${name}")\nif value then\nvalue.`;
	const narrowedTo = (name) => `local value = workspace:FindFirstChild("x")\nif value and value:IsA("${name}") then\nvalue.`;

	const sample = [
		...["Players", "Workspace", "Lighting", "ReplicatedStorage", "TweenService", "RunService", "UserInputService",
			"HttpService", "CollectionService", "PathfindingService"].map((name) => [name, "GetService", serviceOf]),
		...["Frame", "TextLabel", "TextButton", "ScrollingFrame", "ScreenGui", "ImageLabel", "UIListLayout", "TextBox"]
			.map((name) => [name, "Instance.new", newOf]),
		...["HingeConstraint", "SpringConstraint", "AlignPosition", "RopeConstraint", "WeldConstraint", "PrismaticConstraint"]
			.map((name) => [name, "Instance.new", newOf]),
		...["SpawnLocation", "VehicleSeat", "MeshPart", "UnionOperation"].map((name) => [name, "FindFirstChildOfClass", childOf]),
		...["Tool", "Actor", "Humanoid", "Sound"].map((name) => [name, "IsA", narrowedTo]),
		// A class a mod registered with the loader, when this Studio has one.
		...["ModThing"].filter((name) => dump.classes.has(name)).map((name) => [name, "Instance.new", newOf]),
	];

	let pairs = 0;

	for (const [name, form, source] of sample) {
		assert(dump.classes.has(name), `this Studio has no class ${name}; pick another for the sample`);

		const wanted = membersOf(dump.classes, name).filter(expected);
		const got = new Set((await completeAfter(page, source(name))).map((item) => item.label));
		const missing = wanted.filter((member) => !got.has(member.Name));

		let depth = 0;
		for (let current = dump.classes.get(name); current?.parent; current = dump.classes.get(current.parent)) depth++;

		const own = wanted.filter((member) => member.owner === name).length;
		assert(missing.length === 0,
			`${name} via ${form}: ${missing.length} of ${wanted.length} members missing, e.g. ${missing.slice(0, 5).map((member) => `${member.owner}.${member.Name}`).join(", ")}`);

		pairs += wanted.length;
		console.log(`${name} via ${form}: ${wanted.length} of ${wanted.length} members (${own} own, ${wanted.length - own} inherited over ${depth} levels)`);
	}

	console.log(`${sample.length} classes, ${pairs} members: all offered`);

	// The place: an instance reached by its path, then the script's own surroundings.
	const byPath = new Set((await completeAfter(page, "local actor = workspace.RmlMonacoSelfTestActor\nactor:")).map((item) => item.label));
	assert(byPath.has("SendMessage"), "workspace.RmlMonacoSelfTestActor: does not resolve to Actor");
	console.log("workspace.RmlMonacoSelfTestActor: resolves to Actor");

	await completeAfter(page, "");
	await waitForNoPending(page);

	await focus(studio, WORKER_SCRIPT);
	const worker = await studio.pageFor("the worker script", isWorker);

	// The tree that names this script's path is rebuilt a second after its document opens.
	await waitFor("script.Parent to resolve to the Actor the worker is in", async () =>
		(await completeAfter(worker, "local parent = script.Parent\nparent:")).some((item) => item.label === "SendMessage"), 30_000);
	console.log("script.Parent: (a Script inside an Actor) resolves to Actor");

	const own = new Set((await completeAfter(worker, "local actor = script:GetActor()\nif actor then\nactor:")).map((item) => item.label));
	assert(own.has("BindToMessageParallel"), "script:GetActor() does not resolve to Actor");
	console.log("script:GetActor(): resolves to Actor");

	await completeAfter(worker, "");
	await waitForNoPending(worker);
	worker.close();

	await focus(studio, MAIN_SCRIPT);
	await sleep(1500);
}

async function switchEditor(studio, mode, through) {
	if (through === "file") {
		writeFileSync(studio.settingsFile, `# set by tests/mode-and-api.mjs\neditor = "${mode}"\n`);
	} else {
		await studio.selfTest({ action: "menu", monaco: mode === "monaco" });
	}
}

async function checkRoundTrips(studio, page) {
	console.log(`--- ${trips} round trips between Monaco and Studio's editor`);

	for (let trip = 1; trip <= trips; trip++) {
		const nativeBefore = studio.count(NATIVE_IN_CHARGE);
		const toNative = trip % 2 === 1 ? "file" : "menu";
		const toMonaco = trip % 2 === 1 ? "menu" : "file";

		// Every third trip the worker script has an overlay too, so two documents are handed over.
		let workerText;
		if (trip % 3 === 1) {
			await focus(studio, WORKER_SCRIPT);
			const worker = await studio.pageFor("the worker script", isWorker);
			workerText = await typeAtEnd(worker, `-- viagem ${trip}: ação no worker\n`);
			worker.close();
			await focus(studio, MAIN_SCRIPT);
			await sleep(1200);
		}

		// Typed in Monaco, with an accent: a byte and a character are not the same thing there.
		const monacoText = await typeAtEnd(page, `-- viagem ${trip}: digitado no Monaco, não no Studio\n`);

		// On odd trips the switch lands while the engine still owes the editor those keystrokes.
		if (trip % 2 === 0) await waitForNoPending(page);
		page.close();

		const attachedBefore = studio.count(ATTACHED);

		await switchEditor(studio, "native", toNative);
		await waitFor("Studio's editor to take over", () => studio.count(NATIVE_IN_CHARGE) > nativeBefore, 30_000);

		const helpers = await waitFor("the overlay's processes to go away", async () => {
			const left = studio.helperProcesses();
			return left.browsers === 0 && left.servers === 0 && await studio.pageCount() === 0 ? left : undefined;
		}, 30_000);

		const handed = await engineState(studio);
		assert(handed.main.document === monacoText, `trip ${trip}: Studio's editor does not show what Monaco held, ${firstDifference(monacoText, handed.main.document ?? "")}`);
		assert(handed.main.source === monacoText, `trip ${trip}: the script's Source is not what Monaco held, ${firstDifference(monacoText, handed.main.source ?? "")}`);
		if (workerText !== undefined) {
			assert(handed.worker.document === workerText && handed.worker.source === workerText,
				`trip ${trip}: the worker script lost text, ${firstDifference(workerText, handed.worker.document ?? "")}`);
		}

		assert(readFileSync(studio.settingsFile, "utf8").includes('editor = "native"'), `trip ${trip}: the settings file does not say native`);

		// Now the other way: the document changes while Studio's editor has it.
		const marker = `-- viagem ${trip}: escrito pelo Studio\n`;
		await studio.eval(`
			local document = game:GetService("ScriptEditorService"):FindScriptDocument(${MAIN_SCRIPT})
			task.spawn(function()
				local last = document:GetLineCount()
				local column = #document:GetLine(last) + 1
				document:EditTextAsync(${JSON.stringify(marker)}, last, column, last, column)
			end)
			return "queued"
		`);

		const nativeText = await waitFor("the edit made in Studio's editor", async () => {
			const state = await engineState(studio);
			return state.main.document?.endsWith(marker) ? state.main.document : undefined;
		}, 30_000);
		assert(nativeText === monacoText + marker, `trip ${trip}: the edit made in Studio's editor did not land at the end`);

		await switchEditor(studio, "monaco", toMonaco);
		await waitFor("the overlay to come back", () => studio.count(ATTACHED) > attachedBefore, 60_000);

		page = await studio.pageFor("the main script", isMain);

		const shown = await page.evaluate("window.__rmlModelText()");
		assert(shown === nativeText, `trip ${trip}: Monaco does not show what Studio's editor held, ${firstDifference(nativeText, shown)}`);
		assert(await page.evaluate("window.__rmlShownBeforeText()") === false, `trip ${trip}: the overlay was on screen before it had the script's text`);
		assert(readFileSync(studio.settingsFile, "utf8").includes('editor = "monaco"'), `trip ${trip}: the settings file does not say monaco`);

		await page.evaluate("window.__rmlVerify()");
		const engineText = await waitFor("the engine's copy of the document", () => page.evaluate("window.__rmlEngineText"), 30_000);
		assert(engineText === shown, `trip ${trip}: the engine and the editor disagree, ${firstDifference(shown, engineText)}`);

		console.log(`trip ${trip}: to native via ${toNative} (${monacoText.length} chars${workerText === undefined ? "" : `, worker ${workerText.length}`}; ` +
			`browser processes ${helpers.browsers}, language servers ${helpers.servers}, pages 0), ` +
			`back via ${toMonaco} (${shown.length} chars): engine == editor both ways`);
	}

	return page;
}

async function checkBadSettings(studio) {
	console.log("--- a settings file that cannot be used");

	const warnings = () => studio.runLog().split("\n").filter((line) => line.includes("[ScriptEditorWebview]") && line.includes("using the default"));
	const before = warnings().length;

	writeFileSync(studio.settingsFile, 'editor = "vscode"\n');
	await waitFor("the warning about the unknown value", () => warnings().length === before + 1, 15_000);

	writeFileSync(studio.settingsFile, "this is not a settings file\n");
	await waitFor("the warning about the unreadable line", () => warnings().length === before + 2, 15_000);

	// One line per problem, not one per look at the file.
	await sleep(4000);
	const said = warnings().slice(before);
	assert(said.length === 2, `expected one line per problem, the mod logged ${said.length}`);
	for (const line of said) console.log(`  ${line.slice(line.indexOf("[ScriptEditorWebview]"))}`);

	assert(await studio.pageCount() > 0, "a bad settings file took the Monaco overlay down; the default is Monaco");
	console.log("the overlay stayed: an unusable file means the default, Monaco");
}

async function run() {
	const dump = dumpFromStudio(studioDir);
	console.log(`this Studio's API dump: ${dump.classes.size} classes`);

	// Started without a settings file: the mod must say so once, write one, and use Monaco.
	const settings = `${studioDir}/RobloxModLoader/mods/script-editor-webview/script-editor-webview.toml`;
	rmSync(settings, { force: true });

	let studio = await launchStudio({ studioDir, port });
	let unrelated = [];

	const finish = async () => {
		const errors = studio.errors();
		const mine = errors.filter((line) => /ScriptEditorWebview|luau-lsp|WebView2|engine agent|rml\/monaco/i.test(line));
		unrelated = errors.filter((line) => !mine.includes(line));

		assert(mine.length === 0, `the mod logged ${mine.length} error(s):\n${mine.join("\n")}`);
		assert(studio.newCrashDumps().length === 0, `new crash dump(s): ${studio.newCrashDumps().join(", ")}`);
	};

	try {
		await waitFor("the engine agent", () => studio.runLog().includes("engine agent running"), 180_000);
		await waitFor("the overlay to attach", () => studio.runLog().includes(ATTACHED), 120_000);

		assert(studio.count("does not exist; using the default") === 1, "a missing settings file was not reported exactly once");
		assert(existsSync(settings) && readFileSync(settings, "utf8").includes('editor = "monaco"'), "a missing settings file was not written out with the default");
		console.log("no settings file: reported once, written with editor = \"monaco\", Monaco is up");

		let page = await studio.pageFor("the main script", isMain);
		assert(await page.evaluate("window.__rmlShownBeforeText()") === false, "the overlay was on screen before it had the script's text");

		await checkApi(studio, dump, page);
		page.close();

		page = await checkRoundTrips(studio, await studio.pageFor("the main script", isMain));
		page.close();

		await checkBadSettings(studio);

		// The menu repairs the file and the choice outlives Studio.
		const nativeBefore = studio.count(NATIVE_IN_CHARGE);
		await studio.selfTest({ action: "menu", monaco: false });
		await waitFor("Studio's editor to take over", () => studio.count(NATIVE_IN_CHARGE) > nativeBefore, 30_000);
		assert(readFileSync(settings, "utf8").includes('editor = "native"'), "the menu item did not leave a usable settings file");

		await finish();
		const firstRun = unrelated;
		await studio.stop();

		console.log("--- restart with editor = \"native\" in the settings file");
		studio = await launchStudio({ studioDir, port });

		await waitFor("the mod to load", () => studio.runLog().includes("[ScriptEditorWebview] loaded from"), 180_000);
		assert(studio.runLog().includes('editor = "native"'), "the mod did not start in native mode");
		await waitFor("the edit data model", () => studio.runLog().includes("edit data model is live"), 180_000);

		await sleep(15_000);
		const idle = studio.helperProcesses();
		assert(studio.count(ATTACHED) === 0 && studio.count("engine agent running") === 0 && idle.browsers === 0 && idle.servers === 0,
			`native mode after a restart still started something: ${JSON.stringify({ attached: studio.count(ATTACHED), agent: studio.count("engine agent running"), ...idle })}`);
		console.log("15 s in: no overlay, no engine agent, no browser process, no language server");

		await switchEditor(studio, "monaco", "file");
		await waitFor("the overlay to attach", () => studio.count(ATTACHED) > 0, 120_000);
		(await studio.pageFor("the main script", isMain)).close();
		console.log("editor = \"monaco\" written to the file: Monaco came up without a restart");

		await finish();
		unrelated = [...firstRun, ...unrelated];
	} finally {
		await studio.stop();

		// Leave the Studio the way a person would want to find it.
		if (existsSync(settings)) writeFileSync(settings, readFileSync(settings, "utf8").replace(/^editor\s*=.*$/m, 'editor = "monaco"'));
	}

	console.log(`no error line from this mod, no new crash dump${unrelated.length === 0 ? "" : `; ${unrelated.length} error line(s) from other mods:`}`);
	for (const line of new Set(unrelated)) console.log(`  ${line}`);
}

try {
	await run();
	console.log("PASS");
} catch (failure) {
	console.error(`FAIL: ${failure.message}`);
	process.exitCode = 1;
}
