// Fetches what the mod runs but the repository does not hold: Monaco's browser bundle and the
// luau-lsp language server. Both are pinned here, so two machines build the same editor.
//
//   node eng/fetch-payloads.mjs            fetch whatever is missing
//   node eng/fetch-payloads.mjs --force    fetch again, replacing what is there
//
// The Roblox type definitions are a separate step, because they depend on the Studio they are
// for:  node eng/update-roblox-types.mjs --studio <studio directory>

import { spawnSync } from "node:child_process";
import { cpSync, existsSync, mkdirSync, mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const MONACO_VERSION = "0.56.0";
const LUAU_LSP_VERSION = "1.70.0";

const root = join(dirname(fileURLToPath(import.meta.url)), "..");
const force = process.argv.includes("--force");

async function download(url, file) {
	const response = await fetch(url);
	if (!response.ok) throw new Error(`${url} answered ${response.status}`);

	writeFileSync(file, Buffer.from(await response.arrayBuffer()));
}

function extract(archive, into) {
	mkdirSync(into, { recursive: true });

	// Windows ships bsdtar as tar.exe, which reads .tgz and .zip alike. Asked for by full path:
	// a GNU tar earlier in PATH would take "C:" for a remote host.
	const tar = process.platform === "win32" ? join(process.env.SystemRoot ?? "C:/Windows", "System32", "tar.exe") : "tar";
	const run = spawnSync(tar, ["-xf", archive, "-C", into], { encoding: "utf8" });

	if (run.status !== 0) throw new Error(`extracting '${archive}' failed: ${run.stderr || run.error?.message}`);
}

async function fetchMonaco() {
	const target = join(root, "web", "monaco", "vs");
	if (existsSync(join(target, "loader.js")) && !force) return console.log(`monaco: already at '${target}'`);

	const work = mkdtempSync(join(tmpdir(), "rml-monaco-"));
	const archive = join(work, "monaco.tgz");

	await download(`https://registry.npmjs.org/monaco-editor/-/monaco-editor-${MONACO_VERSION}.tgz`, archive);
	extract(archive, work);

	rmSync(target, { recursive: true, force: true });
	cpSync(join(work, "package", "min", "vs"), target, { recursive: true });
	rmSync(work, { recursive: true, force: true });

	console.log(`monaco: ${MONACO_VERSION} at '${target}'`);
}

async function fetchLuauLsp() {
	const target = join(root, "tools", "bin", "luau-lsp.exe");
	if (existsSync(target) && !force) return console.log(`luau-lsp: already at '${target}'`);

	const work = mkdtempSync(join(tmpdir(), "rml-luau-lsp-"));
	const archive = join(work, "luau-lsp.zip");

	await download(
		`https://github.com/JohnnyMorganz/luau-lsp/releases/download/${LUAU_LSP_VERSION}/luau-lsp-win64.zip`, archive);
	extract(archive, work);

	mkdirSync(dirname(target), { recursive: true });
	cpSync(join(work, "luau-lsp.exe"), target);
	rmSync(work, { recursive: true, force: true });

	console.log(`luau-lsp: ${LUAU_LSP_VERSION} at '${target}'`);
}

try {
	await fetchMonaco();
	await fetchLuauLsp();
} catch (failure) {
	console.error(`FAIL: ${failure.message}`);
	process.exitCode = 1;
}
