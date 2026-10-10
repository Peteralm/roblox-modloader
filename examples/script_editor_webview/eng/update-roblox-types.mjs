// Builds the Roblox type definitions the editor completes from, for one particular Studio.
//
//   node eng/update-roblox-types.mjs --studio <studio directory>
//
// Run it again after every Studio update; the mod logs a warning when the Studio it runs in is
// not the one the definitions were built for.
//
// Two sources go in. luau-lsp's own definitions file is the base: it carries the hand-written
// types no dump has (what Instance.new and GetService return, the datatypes, the generics). The
// Studio's API dump, which the Studio binary writes itself, is the truth about what exists: every
// class and member a script can reach that the base does not declare is added from it, typed from
// the dump. That is also what picks up classes that mods register with the loader.
//
// Options: --studio <dir>   Studio to describe (required unless --dump is given)
//          --dump <file>    use this Full-API-Dump.json instead of asking the Studio for one
//          --base <file>    use this definitions file as the base instead of downloading one
//          --base-ref <ref> luau-lsp branch or tag to download the base from (default: main, and
//                           the tag of the bundled luau-lsp if the server cannot read that one)
//          --check          only measure: print the gap of the installed definitions, write nothing

import { spawnSync } from "node:child_process";
import { existsSync, mkdirSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

import { dumpFromStudio, loadDump, measureGap, parseDefinitions, studioVersion } from "./lib/api-dump.mjs";
import { WORKSPACE_ROOT, startServer } from "./lib/luau-lsp-client.mjs";

const root = join(dirname(fileURLToPath(import.meta.url)), "..");
const server = join(root, "tools", "bin", "luau-lsp.exe");
const output = join(root, "tools", "cache", "globalTypes.PluginSecurity.d.luau");
const report = join(root, "tools", "cache", "roblox-types.json");

const args = process.argv.slice(2);
const option = (name) => {
	const at = args.indexOf(`--${name}`);
	return at === -1 ? undefined : args[at + 1];
};

const PRIMITIVES = { bool: "boolean", int: "number", int64: "number", float: "number", double: "number", string: "string" };
const GROUPS = {
	Variant: "any",
	Array: "{ any }",
	Dictionary: "{ [any]: any }",
	Map: "{ [any]: any }",
	Tuple: "...any",
	Function: "((...any) -> ...any)",
};
const KEYWORDS = new Set(["and", "break", "do", "else", "elseif", "end", "false", "for", "function", "if", "in", "local",
	"nil", "not", "or", "repeat", "return", "then", "true", "until", "while", "self"]);

/** A dump type written the way the definitions file writes types; `any` for what it cannot name. */
function luauType(valueType, known) {
	if (!valueType) return "any";

	const optional = valueType.Name.endsWith("?");
	const name = optional ? valueType.Name.slice(0, -1) : valueType.Name;
	let type;

	switch (valueType.Category) {
		case "Primitive":
			type = PRIMITIVES[name] ?? (name === "void" || name === "null" ? "nil" : "any");
			break;
		case "Group":
			type = GROUPS[name] ?? "any";
			break;
		case "Enum":
			type = known.has(`Enum${name}`) ? `Enum${name}` : "any";
			break;
		default:
			type = known.has(name) ? name : "any";
	}

	return optional && type !== "any" && !type.startsWith("...") ? `${type}?` : type;
}

function parameters(list, known) {
	return list.map((parameter, index) => {
		const type = luauType(parameter.Type, known);
		if (type.startsWith("...")) return `...: ${type.slice(3)}`;

		let name = /^[A-Za-z_]\w*$/.test(parameter.Name) ? parameter.Name : `arg${index + 1}`;
		if (KEYWORDS.has(name)) name += "_";

		return `${name}: ${type}${parameter.Default !== undefined && !type.endsWith("?") && type !== "any" ? "?" : ""}`;
	});
}

function returns(member, known) {
	const list = Array.isArray(member.ReturnType) ? member.ReturnType : [member.ReturnType];
	const types = list.filter(Boolean).map((type) => luauType(type, known));

	if (types.length === 0 || (types.length === 1 && types[0] === "nil")) return "()";

	return types.length === 1 ? types[0] : `(${types.join(", ")})`;
}

/** One member as lines of a `declare extern type` block. */
function declaration(member, known) {
	const key = /^[A-Za-z_]\w*$/.test(member.Name) ? member.Name : `[${JSON.stringify(member.Name)}]`;

	switch (member.MemberType) {
		case "Function": {
			const line = `function ${member.Name}(${["self", ...parameters(member.Parameters, known)].join(", ")}): ${returns(member, known)}`;
			return member.deprecated ? ["\t@deprecated", `\t\t${line}`] : [`\t${line}`];
		}
		case "Event":
			return [`\t${key}: RBXScriptSignal<${parameters(member.Parameters, known).map((text) => text.replace(/^[^:]+: /, "")).join(", ")}>`];
		case "Callback":
			return [`\t${key}: (${parameters(member.Parameters, known).join(", ")}) -> ${returns(member, known)}`];
		default:
			return [`\t${key}: ${luauType(member.ValueType, known)}`];
	}
}

/**
 * The definitions syntax can mark a method as deprecated and nothing else. A deprecated property,
 * event or callback added from the dump would look as current as any other member, so those stay
 * out, which is also what luau-lsp does in VS Code.
 */
function addable(member) {
	return !member.deprecated || (member.MemberType === "Function" && /^[A-Za-z_]\w*$/.test(member.Name));
}

function patch(baseText, dump) {
	const definitions = parseDefinitions(baseText);
	const gap = measureGap(dump, definitions);
	const known = new Set([...definitions.types, ...gap.missingClasses]);

	const additions = new Map();
	let added = 0;
	let omitted = 0;

	for (const { owner, member } of gap.missingMembers) {
		if (!addable(member)) {
			omitted++;
			continue;
		}

		if (!additions.has(owner)) additions.set(owner, []);
		additions.get(owner).push(...declaration(member, known));
		added++;
	}

	const lines = baseText.split("\n");
	const result = [];
	const moved = [];
	const misparented = new Set(gap.misparented);
	let current;
	let target = result;

	for (const line of lines) {
		const header = /^declare (?:extern type|class) (\w+)/.exec(line);
		if (header) {
			current = header[1];

			// A class this Studio hangs under another parent gets that parent, and its block goes
			// to the end of the file: a class may only extend one declared above it.
			if (misparented.has(current)) {
				const parent = dump.classes.get(current).parent;
				target = moved;
				target.push(`declare extern type ${current}${parent ? ` extends ${parent}` : ""} with`);
				continue;
			}
		}

		if (/^end\b/.test(line) && current) {
			if (additions.has(current)) target.push(...additions.get(current));
			target.push(line);

			if (target === moved) moved.push("");
			current = undefined;
			target = result;
			continue;
		}

		target.push(line);
	}

	result.push(...moved);

	for (const name of gap.missingClasses) {
		const entry = dump.classes.get(name);
		result.push(`declare extern type ${name}${entry.parent ? ` extends ${entry.parent}` : ""} with`,
			...(additions.get(name) ?? []), "end", "");
	}

	// The first line lists the classes Instance.new and GetService accept; a class that is not in
	// it is one the server refuses to construct, whatever is declared further down.
	if (gap.missingClasses.length > 0 && result[0].startsWith("--#METADATA#")) {
		const metadata = JSON.parse(result[0].slice("--#METADATA#".length));

		for (const name of gap.missingClasses) {
			const entry = dump.classes.get(name);
			if (entry.service) metadata.SERVICES.push(name);
			else if (entry.creatable) metadata.CREATABLE_INSTANCES.push(name);
		}

		result[0] = `--#METADATA#${JSON.stringify(metadata)}`;
	}

	return { text: result.join("\n"), gap, added, omitted };
}

/** A definitions file the server cannot read costs every Roblox type at once, so it is tried first. */
async function serverAccepts(definitions) {
	const lsp = await startServer({ executable: server, definitions });

	try {
		const items = await lsp.completeAtEnd(`${WORKSPACE_ROOT}/main.luau`, 'local actor = Instance.new("Actor")\nactor:');
		const refused = lsp.messages.find((message) => message.params?.message?.startsWith("Failed to read definitions file"));

		if (refused) return refused.params.message;
		if (!items.some((item) => item.label === "SendMessage")) return "Instance.new(\"Actor\") does not complete to Actor's methods";

		return undefined;
	} finally {
		await lsp.stop();
	}
}

async function downloadBase(ref) {
	const url = `https://raw.githubusercontent.com/JohnnyMorganz/luau-lsp/${ref}/scripts/globalTypes.PluginSecurity.d.luau`;
	const response = await fetch(url);
	if (!response.ok) throw new Error(`${url} answered ${response.status}`);

	return response.text();
}

function describe(label, measured, omitted) {
	const missing = measured.missingMembers.length;
	console.log(`${label}: ${measured.classes - measured.missingClasses.length} of ${measured.classes} classes, ` +
		`${measured.members - missing} of ${measured.members} members a script can reach` +
		(omitted === undefined ? "" : ` (${omitted} deprecated properties, events and callbacks left out on purpose)`));
}

async function run() {
	const studio = option("studio");
	if (!studio && !option("dump")) throw new Error("pass --studio <studio directory> or --dump <Full-API-Dump.json>");

	const dump = option("dump") ? loadDump(option("dump")) : dumpFromStudio(studio);
	const version = studio ? studioVersion(studio) : "unknown";
	console.log(`Studio ${version}: ${dump.classes.size} classes in its API dump`);

	if (args.includes("--check")) {
		if (!existsSync(output)) throw new Error(`no definitions at '${output}'`);

		const measured = measureGap(dump, parseDefinitions(readFileSync(output, "utf8")));
		const omitted = measured.missingMembers.filter(({ member }) => !addable(member)).length;
		describe("installed definitions", measured, omitted);

		if (measured.misparented.length > 0) console.log(`under another parent than in this Studio: ${measured.misparented.join(", ")}`);

		if (measured.missingClasses.length > 0 || measured.misparented.length > 0 || measured.missingMembers.length > omitted) {
			process.exitCode = 1;
		}
		return;
	}

	if (!existsSync(server)) throw new Error(`no luau-lsp at '${server}'; run: node eng/fetch-payloads.mjs`);

	const bundled = spawnSync(server, ["--version"], { encoding: "utf8" }).stdout.trim();
	const candidates = option("base")
		? [{ ref: option("base"), read: async () => readFileSync(option("base"), "utf8") }]
		: [option("base-ref") ?? "main", bundled].map((ref) => ({ ref, read: () => downloadBase(ref) }));

	mkdirSync(dirname(output), { recursive: true });
	const staging = `${output}.new`;

	for (const candidate of candidates) {
		const base = (await candidate.read()).replaceAll("\r\n", "\n");
		const patched = patch(base, dump);

		writeFileSync(staging, patched.text);
		const refusal = await serverAccepts(staging);
		rmSync(staging);

		if (refusal) {
			console.log(`base '${candidate.ref}' is not usable with luau-lsp ${bundled}: ${refusal}`);
			continue;
		}

		const after = measureGap(dump, parseDefinitions(patched.text));

		describe(`base '${candidate.ref}'`, patched.gap);
		describe("written", after, patched.omitted);
		if (patched.gap.missingClasses.length > 0) console.log(`classes added: ${patched.gap.missingClasses.join(", ")}`);
		if (patched.gap.misparented.length > 0) console.log(`classes moved under this Studio's parent: ${patched.gap.misparented.join(", ")}`);
		if (after.misparented.length > 0) throw new Error(`still under the wrong parent: ${after.misparented.join(", ")}`);

		writeFileSync(output, patched.text);
		writeFileSync(report, `${JSON.stringify({
			studioVersion: version,
			base: candidate.ref,
			luauLsp: bundled,
			classes: after.classes,
			members: after.members,
			membersDeclared: after.members - after.missingMembers.length,
			addedFromDump: patched.added,
			classesAddedFromDump: patched.gap.missingClasses,
			classesReparented: patched.gap.misparented,
			deprecatedLeftOut: patched.omitted,
		}, null, "\t")}\n`);

		console.log(`wrote '${output}'`);
		return;
	}

	throw new Error("no base definitions file the bundled luau-lsp can read; the installed definitions were left alone");
}

try {
	await run();
} catch (failure) {
	console.error(`FAIL: ${failure.message}`);
	process.exitCode = 1;
}
