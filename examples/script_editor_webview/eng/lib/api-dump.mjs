// What the engine exposes to scripts, read from a Studio API dump, and what a luau-lsp definitions
// file declares. Both the generator (eng/update-roblox-types.mjs) and the coverage check
// (tests/api-coverage.mjs) count with these, so "the gap" means the same thing in both.

import { spawnSync } from "node:child_process";
import { existsSync, mkdtempSync, readFileSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";

// The definitions file luau-lsp ships is the PluginSecurity one: everything a game script or a
// plugin can touch. Members above that level only exist for Roblox's own scripts.
const REACHABLE_SECURITY = new Set(["None", "PluginSecurity"]);

/**
 * Asks a Studio binary for its own API dump, so the data always matches the build it describes,
 * and reads it. The binary writes the file and exits without opening a window.
 */
export function dumpFromStudio(studioDir) {
	const executable = join(studioDir, "RobloxStudioBeta.exe");
	if (!existsSync(executable)) throw new Error(`no Studio at '${executable}'`);

	const scratch = mkdtempSync(join(tmpdir(), "rml-api-dump-"));

	try {
		const file = join(scratch, "Full-API-Dump.json");
		const run = spawnSync(executable, ["-FullAPI", file], { cwd: studioDir, timeout: 180_000 });

		if (run.error) throw new Error(`running '${executable} -FullAPI' failed: ${run.error.message}`);
		if (!existsSync(file)) throw new Error(`'${executable} -FullAPI' exited with ${run.status} and wrote no dump`);

		return loadDump(file);
	} finally {
		rmSync(scratch, { recursive: true, force: true });
	}
}

/** File version of a Studio binary, e.g. "0.739.0.7390687". */
export function studioVersion(studioDir) {
	const executable = join(studioDir, "RobloxStudioBeta.exe");
	const run = spawnSync("powershell", ["-NoProfile", "-Command",
		`(Get-Item -LiteralPath '${executable.replaceAll("'", "''")}').VersionInfo.FileVersion`], { encoding: "utf8" });

	const version = (run.stdout ?? "").trim().replaceAll(", ", ".");
	if (!/^\d+\.\d+\.\d+\.\d+$/.test(version)) throw new Error(`could not read the file version of '${executable}'`);

	return version;
}

function securityOf(member) {
	if (typeof member.Security === "string") return member.Security;

	// A property is reachable when a script can at least read it.
	return member.Security?.Read ?? "None";
}

function tagsOf(member) {
	return (member.Tags ?? []).filter((tag) => typeof tag === "string");
}

/**
 * The dump as a class table: every class with its parent and the members a script can reach
 * (security None or PluginSecurity, not tagged NotScriptable). Deprecated and hidden members are
 * kept: a script can still call them, and the editor shows them struck through.
 */
export function loadDump(file) {
	const dump = JSON.parse(readFileSync(file, "utf8"));
	const classes = new Map();

	for (const entry of dump.Classes) {
		const members = [];

		for (const member of entry.Members) {
			if (!REACHABLE_SECURITY.has(securityOf(member))) continue;

			const tags = tagsOf(member);
			if (tags.includes("NotScriptable")) continue;

			members.push({ ...member, tags, deprecated: tags.includes("Deprecated") });
		}

		const tags = tagsOf(entry);
		classes.set(entry.Name, {
			name: entry.Name,
			parent: entry.Superclass === "<<<ROOT>>>" ? undefined : entry.Superclass,
			members,
			service: tags.includes("Service"),
			creatable: !tags.includes("NotCreatable") && !tags.includes("Service"),
		});
	}

	return { classes, enums: new Set(dump.Enums.map((entry) => entry.Name)) };
}

/** Own and inherited reachable members of one class, nearest class first. */
export function membersOf(classes, className) {
	const seen = new Map();

	for (let current = classes.get(className); current; current = classes.get(current.parent)) {
		for (const member of current.members) {
			if (!seen.has(member.Name)) seen.set(member.Name, { ...member, owner: current.name });
		}
	}

	return [...seen.values()];
}

/**
 * The classes a definitions file declares, each with its parent and the member names in its block.
 * A deprecated member sits one tab deeper, under its attribute line, and the hand-written blocks
 * are indented with spaces, so indentation is not fixed.
 */
export function parseDefinitions(text) {
	const declared = new Map();
	const types = new Set();
	let current;

	for (const line of text.split("\n")) {
		const header = /^declare (?:extern type|class) (\w+)(?: extends (\w+))?/.exec(line);
		if (header) {
			current = { parent: header[2], members: new Set() };
			declared.set(header[1], current);
			types.add(header[1]);
			continue;
		}

		const alias = /^(?:export )?type (\w+)/.exec(line);
		if (alias) types.add(alias[1]);

		if (/^end\b/.test(line)) {
			current = undefined;
			continue;
		}

		if (!current) continue;

		const member = /^[\t ]+(?:function (\w+)\(|(\w+):|\[("(?:[^"\\]|\\.)*")\]:)/.exec(line);
		if (member) current.members.add(member[1] ?? member[2] ?? JSON.parse(member[3]));
	}

	return { declared, types };
}

/** True when the class, or one it inherits from, declares the member. */
export function declares(declared, className, memberName) {
	for (let current = declared.get(className); current; current = declared.get(current.parent)) {
		if (current.members.has(memberName)) return true;
	}

	return false;
}

/**
 * Every reachable member the definitions do not declare, every class they do not know, and every
 * class they hang under a different parent than this Studio does (Roblox moves classes around
 * between builds, and a wrong parent hides or invents everything inherited).
 */
export function measureGap(dump, definitions) {
	const missingClasses = [];
	const missingMembers = [];
	const misparented = [];
	let members = 0;

	for (const entry of dump.classes.values()) {
		const declared = definitions.declared.get(entry.name);

		if (!declared) missingClasses.push(entry.name);
		else if (declared.parent !== entry.parent) misparented.push(entry.name);

		for (const member of entry.members) {
			members++;
			if (!declares(definitions.declared, entry.name, member.Name)) missingMembers.push({ owner: entry.name, member });
		}
	}

	return { classes: dump.classes.size, members, missingClasses, missingMembers, misparented };
}
