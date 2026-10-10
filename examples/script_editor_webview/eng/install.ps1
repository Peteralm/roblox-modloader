# Builds the mod and installs it into one Studio.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File eng/install.ps1 -StudioDir C:\b\studio-test
#
# The settings file (script-editor-webview.toml) of an existing install is kept. The previous
# install is moved to -BackupDir, which must stay outside the Studio's mods folder: the loader
# treats every folder in there as a mod, and a backup left inside it loads as a second copy.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$StudioDir,
    [string]$BuildDir = "C:\b\rml-script-editor-mode-api",
    [string]$BackupDir = "C:\b\rmlmods-backups"
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$modRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$studioExe = Join-Path $StudioDir "RobloxStudioBeta.exe"
if (-not (Test-Path $studioExe)) {
    throw "StudioDir does not contain RobloxStudioBeta.exe: $StudioDir"
}

$running = Get-Process RobloxStudioBeta -ErrorAction SilentlyContinue |
    Where-Object { $_.Path -and ([System.IO.Path]::GetFullPath($_.Path) -eq [System.IO.Path]::GetFullPath($studioExe)) }
if ($running) {
    throw "Close the Roblox Studio at $StudioDir first: it holds the mod's files open."
}

foreach ($payload in @("web\monaco\vs\loader.js", "tools\bin\luau-lsp.exe")) {
    if (-not (Test-Path (Join-Path $modRoot $payload))) {
        throw "Missing $payload. Fetch it with: node eng/fetch-payloads.mjs"
    }
}

if (-not (Test-Path (Join-Path $modRoot "tools\cache\globalTypes.PluginSecurity.d.luau"))) {
    throw "Missing the Roblox type definitions. Build them with: node eng/update-roblox-types.mjs --studio `"$StudioDir`""
}

dotnet build (Join-Path $modRoot "src\ScriptEditorWebview\ScriptEditorWebview.csproj") -c Release `
    --artifacts-path (Join-Path $BuildDir "artifacts") -nologo -v q
if ($LASTEXITCODE -ne 0) {
    throw "dotnet build failed."
}

$built = Join-Path $BuildDir "artifacts\bin\ScriptEditorWebview\release_win-x64"
$target = Join-Path $StudioDir "RobloxModLoader\mods\script-editor-webview"
$settings = Join-Path $target "script-editor-webview.toml"
$keptSettings = if (Test-Path $settings) { Get-Content -Raw -LiteralPath $settings } else { $null }
$keptManifest = if (Test-Path (Join-Path $target "mod.toml")) { Get-Content -Raw -LiteralPath (Join-Path $target "mod.toml") } else { $null }

if (Test-Path $target) {
    New-Item -ItemType Directory -Force $BackupDir | Out-Null
    $backup = Join-Path $BackupDir ("script-editor-webview." + (Get-Date -Format "yyyyMMdd-HHmmss"))
    Move-Item -LiteralPath $target -Destination $backup
    Write-Host "previous install moved to $backup"
}

New-Item -ItemType Directory -Force (Join-Path $target "dotnet") | Out-Null
Copy-Item -Path (Join-Path $built "*") -Destination (Join-Path $target "dotnet") -Recurse -Force

# Written through .NET: Set-Content -Encoding utf8 would put a byte-order mark at the top.
if ($null -ne $keptManifest) {
    [System.IO.File]::WriteAllText((Join-Path $target "mod.toml"), $keptManifest)
} else {
    $manifest = (Get-Content -Raw -LiteralPath (Join-Path $modRoot "eng\mod.toml")).TrimEnd() + @'


[runtime]
enabled = true
auto_load = true
priority = 500
managed_entry = "ScriptEditorWebview.dll"
'@
    [System.IO.File]::WriteAllText((Join-Path $target "mod.toml"), $manifest + "`n")
}

if ($null -ne $keptSettings) {
    [System.IO.File]::WriteAllText($settings, $keptSettings)
}

Write-Host "installed script-editor-webview into $target"
