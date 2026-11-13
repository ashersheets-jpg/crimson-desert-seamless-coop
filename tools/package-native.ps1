# Builds the SHARED-WORLD package (the big update's B01, docs/design/native-join-and-saves.md):
#   dist\CrimsonDesert-SeamlessCoop-<version>-shared-world.zip  (+ -mod-manager.zip, + -symbols-KEEP-PRIVATE.zip)
#
#   .\tools\package-native.ps1              # builds the normal package first (package-coopplus.ps1), then this one
#   .\tools\package-native.ps1 -SkipNormal  # reuse the normal package's stage already in dist\
#
# Made from the normal package's stage (installer, README, licences, scaling.toml, the player's Co-op+ settings), with:
#   mod\cdmp.asi      = the native base (ByJunter's NativeServer, ported to 3663, upstream/cdmp-native, GPL-3.0)
#   mod\coopplus.asi  = the Co-op+ layer (upstream/cd-coop-3446 built with -DCDMP_NATIVE_LAYER=ON)
#   mod\coopplus.ini  = the player's Co-op+ settings (the normal package's cdmp.ini)
#   mod\cdmp.ini      = Co-op+'s launch-mode file for the native base, Solo (net/native_mode_text.h writes the others)
#   source-native.zip = the native base's GPL source of exactly this build
# Publishing it publishes ByJunter's GPL code: the user decides (NOTES/FLAGS.md F23).
param([switch]$SkipNormal, [string]$Config = 'RelWithDebInfo')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$up = Join-Path $root 'upstream\cd-coop-3446'
$nat = Join-Path $root 'upstream\cdmp-native'
$cmake = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'

if (-not $SkipNormal) {
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'package-coopplus.ps1')
    if ($LASTEXITCODE -ne 0) { throw 'The normal package failed' }
}
$normal = Get-ChildItem (Join-Path $root 'dist') -Directory -Filter 'CrimsonDesert-SeamlessCoop-*' |
    Where-Object { $_.Name -notmatch 'mod-manager|shared-world' } | Sort-Object LastWriteTime | Select-Object -Last 1
if (-not $normal) { throw 'No normal package stage in dist\ (run without -SkipNormal)' }

# The native base must be committed (its build id names the commit) and both DLLs built now.
$natDirty = & git -C $nat status --porcelain -- src
if ($natDirty) { throw "Uncommitted changes in upstream\cdmp-native\src:`n$($natDirty -join "`n")" }
foreach ($b in @(@{ dir = (Join-Path $nat 'build'); what = 'native base' }, @{ dir = (Join-Path $up 'build-native-layer'); what = 'Co-op+ layer' })) {
    # (PowerShell 5.1: with Stop, a line the build writes to stderr would end the script here)
    $ErrorActionPreference = 'Continue'
    $log = & $cmake --build $b.dir --config $Config --target cdmp_mod -- -m '/v:minimal' 2>&1
    $ErrorActionPreference = 'Stop'
    if ($LASTEXITCODE -ne 0) { throw "Build failed: $($b.what)`n$(($log | Select-Object -Last 15) -join "`n")" }
}
$natBin = Join-Path $nat "build\bin\$Config"
$layerBin = Join-Path $up "build-native-layer\bin\$Config"
foreach ($f in (Join-Path $natBin 'cdmp.asi'), (Join-Path $natBin 'cdmp.pdb'), (Join-Path $layerBin 'coopplus.asi'), (Join-Path $layerBin 'coopplus.pdb')) {
    if (-not (Test-Path $f)) { throw "Missing: $f" }
}

$name = "$($normal.Name)-shared-world"
$out = Join-Path $root 'dist'
$stage = Join-Path $out $name
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
Copy-Item $normal.FullName $stage -Recurse
Remove-Item (Join-Path $stage 'SHA256.txt') -ErrorAction SilentlyContinue
Move-Item (Join-Path $stage 'mod\cdmp.ini') (Join-Path $stage 'mod\coopplus.ini')
Copy-Item (Join-Path $natBin 'cdmp.asi') (Join-Path $stage 'mod\cdmp.asi') -Force
Copy-Item (Join-Path $layerBin 'coopplus.asi') (Join-Path $stage 'mod\coopplus.asi')
# Solo, as net/native_mode_text.h writes it (UTF-16 LE with BOM, CRLF).
$solo = @(
    '; Written by Co-op+ (coopplus.asi): the native base''s mode for this launch. Co-op+ rewrites this file;',
    '; edit coopplus.ini instead.', '; coopplus_mode=solo', '[general]', 'player_name=Player', '[net]', 'enabled=0',
    '[overlay]', 'enabled=0', '[coop]', 'enabled=0', '[session2]', 'enabled=0', '[sniffer]', 'record=0') -join "`r`n"
[IO.File]::WriteAllText((Join-Path $stage 'mod\cdmp.ini'), $solo + "`r`n", (New-Object Text.UnicodeEncoding($false, $true)))
# The native base's GPL source (its own repository, this commit).
$natSha = (& git -C $nat rev-parse --short=8 HEAD).Trim()
& git -C $nat archive --format=zip -o (Join-Path $stage 'source-native.zip') HEAD -- src CMakeLists.txt cmake LICENSE README.md third_party scripts tests tools
if ($LASTEXITCODE -ne 0) { throw 'git archive of the native base failed' }
Add-Content (Join-Path $stage 'README.txt') @"

SHARED WORLD BUILD
------------------
This build plays co-op in ONE shared world: your friend joins your actual world (the native base by ByJunter,
CrimsonDesertMultiplayer NativeServer, ported to this game version) instead of seeing copies. Start Co-op in the
title menu restarts both games once into co-op (about 30 seconds); the next launch is your own game again.
Your own saves are never written in co-op: each friend has a co-op save of its own in
%LOCALAPPDATA%\Pearl Abyss\CD\coop_worlds.
Your Co-op+ settings are in coopplus.ini (cdmp.ini is Co-op+'s own launch file now).
Native base source: source-native.zip (GPL-3.0, commit $natSha).
"@

Get-ChildItem $stage -File -Recurse | Where-Object Name -ne 'SHA256.txt' | ForEach-Object {
    '{0}  {1}' -f (Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLower(), $_.FullName.Substring($stage.Length + 1)
} | Set-Content -Encoding ascii (Join-Path $stage 'SHA256.txt')
$zip = Join-Path $out "$name.zip"
if (Test-Path $zip) { Remove-Item $zip }
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip

$mstage = Join-Path $out "$name-mod-manager"
if (Test-Path $mstage) { Remove-Item -Recurse -Force $mstage }
New-Item -ItemType Directory -Force (Join-Path $mstage 'bin64') | Out-Null
Copy-Item (Join-Path $stage 'mod\*') (Join-Path $mstage 'bin64') -Recurse
foreach ($f in 'README.txt', 'LICENSE.txt', 'CREDITS.md', 'THIRD-PARTY-NOTICES.txt') {
    Copy-Item (Join-Path $stage $f) (Join-Path $mstage "bin64\CoopPlus-$f")
}
$manager = Join-Path $out "$name-mod-manager.zip"
if (Test-Path $manager) { Remove-Item $manager }
Compress-Archive -Path (Join-Path $mstage 'bin64') -DestinationPath $manager

$symbols = Join-Path $out "$name-symbols-KEEP-PRIVATE.zip"
if (Test-Path $symbols) { Remove-Item $symbols }
$sym = Join-Path $out "$name-symbols"
if (Test-Path $sym) { Remove-Item -Recurse -Force $sym }
New-Item -ItemType Directory -Force $sym | Out-Null
Copy-Item (Join-Path $natBin 'cdmp.asi'), (Join-Path $natBin 'cdmp.pdb') $sym
Copy-Item (Join-Path $layerBin 'coopplus.asi'), (Join-Path $layerBin 'coopplus.pdb') $sym
Compress-Archive -Path (Join-Path $sym '*') -DestinationPath $symbols
Remove-Item -Recurse -Force $sym

Write-Host "Shared-world package: $zip ($([math]::Round((Get-Item $zip).Length / 1KB)) KB)"
Write-Host "Mod-manager zip: $manager"
Write-Host "Symbols (keep, do not publish): $symbols"
