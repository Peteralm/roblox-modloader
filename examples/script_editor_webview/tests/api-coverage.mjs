// Measures what the editor's language server knows about the instance API against what a Studio
// actually has, class by class, without starting Studio's window: the Studio binary writes its own
// API dump, and luau-lsp is asked for completions on every class in it, over the same protocol and
// settings the mod uses.
//
//   node tests/api-coverage.mjs --studio C:/b/studio-test
//
// It fails when a member a script can reach is missing from the completions of its class or of any
// class that inherits it, and when one of the ways a script gets hold of an instance (Instance.new,
// GetService, FindFirstChildOfClass, IsA, a path through the place, `script`) stops resolving to
// the class. Deprecated properties, events and callbacks are left out on purpose and counted.
//
// Options: --studio <dir>      Studio to compare against (required unless --dump is given)
//          --dump <file>       a Full-API-Dump.json to use instead of asking the Studio
//          --definitions <f>   definitions file to check (default: tools/cache in this folder)
//          --as-shipped        talk to the server the way the mod did before it was fixed (tree sent
//                              before the document, no script paths, fragment autocomplete left on),
//                              to measure what that cost

import { existsSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

import { dumpFromStudio, loadDump, membersOf } from "../eng/lib/api-dump.mjs";
import { WORKSPACE_ROOT, startServer } from "../eng/lib/luau-lsp-client.mjs";

const root = join(dirname(fileURLToPath(import.meta.url)), "..");
const args = process.argv.slice(2);
const option = (name, fallback) => {
	const at = args.indexOf(`--${name}`);
	return at === -1 ? fallback : args[at + 1];
};

const executable = join(root, "tools", "bin", "luau-lsp.exe");
const definitions = option("definitions", join(root, "tools", "cache", "globalTypes.PluginSecurity.d.luau"));
const asShipped = args.includes("--as-shipped");

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

/** Mirrors eng/update-roblox-types.mjs: what the definitions syntax cannot mark as deprecated stays out. */
const expected = (member) => !member.deprecated || member.MemberType === "Function";

async function run() {
	for (const file of [executable, definitions]) {
		if (!existsSync(file)) throw new Error(`'${file}' is missing; run eng/fetch-payloads.mjs and eng/update-roblox-types.mjs`);
	}

	const studio = option("studio");
	if (!studio && !option("dump")) throw new Error("pass --studio <studio directory> or --dump <Full-API-Dump.json>");

	const dump = option("dump") ? loadDump(option("dump")) : dumpFromStudio(studio);
	const server = await startServer({ executable, definitions, fragmentAutocomplete: asShipped });
	const failures = [];

	try {
		const uri = asShipped ? `${WORKSPACE_ROOT}/main.luau` : `${WORKSPACE_ROOT}/scripts/1.luau`;

		// The place, as the engine agent describes it: a worker script inside an Actor.
		const place = {
			Name: "game",
			ClassName: "DataModel",
			Children: [{
				Name: "Workspace",
				ClassName: "Workspace",
				Children: [{
					Name: "Engine",
					ClassName: "Actor",
					Children: [{ Name: "Worker", ClassName: "Script", ...(asShipped ? {} : { FilePaths: ["scripts/1.luau"] }) }],
				}],
			}],
		};

		// The mod now sends the tree once the document is open, with a file path on the script the
		// document belongs to. It used to send it right after the handshake, without paths.
		if (!asShipped) server.setText(uri, "");
		server.pushDataModel(place);
		await sleep(500);

		// A class name retyped on an earlier line, one key at a time and with nothing asked in
		// between, the way a person does it: the list on the line below must follow. With
		// luau-lsp's fragment autocomplete left on, it keeps answering with the members of the
		// class that was there before.
		const retyped = (name) => `local thing = Instance.new("${name}")\nthing:`;
		const listed = async (text) => new Set((await server.completeAtEnd(uri, text)).map((item) => item.label));

		await listed(retyped("Part"));
		for (const partial of ["Par", "Pa", "P", "", "S", "Se", "Sea"]) {
			server.setText(uri, retyped(partial));
			await sleep(120);
		}
		await sleep(1500);

		const followed = (await listed(retyped("Seat"))).has("Sit");
		console.log(`a class name retyped on the line above: the list ${followed ? "follows it" : "is STALE, still the old class"}`);
		if (!followed) failures.push("after Part is retyped as Seat on the line above, the list is still Part's");

		// Every probe sits one line lower than the one before. With fragment autocomplete on, a
		// probe at the cursor position of an earlier one would be answered from that earlier
		// document; that is a fault of its own, measured just above, and must not leak into the
		// count of what the definitions hold.
		let probes = 0;
		const labels = async (text) =>
			new Set((await server.completeAtEnd(uri, "\n".repeat(++probes) + text)).map((item) => item.label));

		// Every class, through a plain annotation: own members and everything inherited.
		let classesComplete = 0;
		let membersWanted = 0;
		let membersFound = 0;
		let leftOut = 0;

		for (const entry of dump.classes.values()) {
			const all = membersOf(dump.classes, entry.name);
			const wanted = all.filter(expected);
			leftOut += entry.members.length - entry.members.filter(expected).length;

			const got = await labels(`local value: ${entry.name} = nil :: any\nvalue.`);
			const missing = wanted.filter((member) => !got.has(member.Name));

			membersWanted += wanted.length;
			membersFound += wanted.length - missing.length;

			if (missing.length === 0) classesComplete++;
			else failures.push(`${entry.name}: ${missing.length} of ${wanted.length} members missing, e.g. ${missing.slice(0, 5).map((member) => `${member.owner}.${member.Name}`).join(", ")}`);
		}

		console.log(`classes: ${classesComplete} of ${dump.classes.size} complete with every own and inherited member`);
		console.log(`members: ${membersFound} of ${membersWanted} class-member pairs answered (own and inherited)`);
		console.log(`left out on purpose: ${leftOut} deprecated properties, events and callbacks`);

		// The ways a script reaches an instance. Each must resolve to the class, which is shown by a
		// member only that class has.
		const creatable = [...dump.classes.values()].filter((entry) => entry.creatable);
		const services = [...dump.classes.values()].filter((entry) => entry.service);
		const own = (entry) => entry.members.filter(expected).find((member) => /^[A-Za-z_]\w*$/.test(member.Name))?.Name;

		const forms = [
			["Instance.new", creatable, (name) => `local value = Instance.new("${name}")\nvalue.`],
			["game:GetService", services, (name) => `local value = game:GetService("${name}")\nvalue.`],
			["FindFirstChildOfClass", creatable, (name) => `local value = workspace:FindFirstChildOfClass("${name}")\nif value then\nvalue.`],
			["IsA narrowing", creatable, (name) => `local value = workspace:FindFirstChild("x")\nif value and value:IsA("${name}") then\nvalue.`],
		];

		for (const [form, classes, source] of forms) {
			const probes = classes.filter(own);
			let resolved = 0;

			for (const entry of probes) {
				if ((await labels(source(entry.name))).has(own(entry))) resolved++;
				else failures.push(`${form}("${entry.name}") does not complete ${entry.name}.${own(entry)}`);
			}

			console.log(`${form}: ${resolved} of ${probes.length} classes with a member of their own resolve`);
		}

		// The place itself: an instance reached by its path, and the script's own surroundings.
		const placeForms = [
			["workspace.Engine", "local value = workspace.Engine\nvalue:"],
			["script.Parent", "local value = script.Parent\nvalue:"],
			["script:GetActor()", "local value = script:GetActor()\nif value then\nvalue:"],
		];

		for (const [form, source] of placeForms) {
			const got = await labels(source);
			const actor = ["SendMessage", "BindToMessage", "BindToMessageParallel"].every((name) => got.has(name));

			console.log(`${form}: ${actor ? "resolves to Actor" : "does NOT resolve to Actor"}`);
			if (!actor) failures.push(`${form} does not complete Actor's methods`);
		}
	} finally {
		await server.stop();
	}

	if (failures.length > 0) {
		throw new Error(`${failures.length} gap(s):\n${failures.slice(0, 40).join("\n")}`);
	}
}

try {
	await run();
	console.log("PASS");
} catch (failure) {
	console.error(`FAIL: ${failure.message}`);
	process.exitCode = 1;
}
