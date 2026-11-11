# Crimson Desert Seamless Co-op+ diagnostics. Started by "Diagnose Co-op+.cmd".
# Writes CoopPlus-report-<date>.txt and CoopPlus-report-<date>.zip (the report plus the whole cdmp.log and coopplus.log) to the
# Desktop and puts the zip on the clipboard, ready to paste into a message. Send it when something goes wrong.
# Reads only; changes nothing in the game. Co-op+ (T37): personal data is taken out first: Steam names (yours and
# your friend's), SteamIDs, lobby ids, room codes, your Windows user and PC name, IP addresses, e-mail addresses.
# -NoRedact keeps everything (only when someone asks for it).

param(
    [string]$GameDir = '',
    [int]$LogLines = 300,
    [string]$OutDir = '',      # default: the Desktop
    [switch]$NoRedact,
    [switch]$NoClipboard,
    [switch]$NoPause
)
$ErrorActionPreference = 'Continue'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $here 'coopplus-common.ps1')

$out = New-Object System.Collections.Generic.List[string]

# Co-op+ (T37): what is personal in a log, and what replaces it.
$names = New-Object System.Collections.Generic.List[string]
function Learn-Names([string[]]$lines) {
    $patterns = @("playing as '([^']+)'", "Config: player '([^']+)'", "connected to '([^']+)'", "connecting to '([^']+)'",
                  "changed from '([^']*)' to '([^']+)'", "while '([^']+)' is in the session", "^player_name=(.+)$")
    foreach ($l in $lines) {
        foreach ($p in $patterns) {
            $m = [regex]::Match($l, $p)
            for ($g = 1; $m.Success -and $g -lt $m.Groups.Count; ++$g) {
                $n = $m.Groups[$g].Value.Trim()
                if ($n.Length -ge 2 -and $n -notmatch '^(Player|cdmp_peer.*|\?)$' -and -not $names.Contains($n)) { $names.Add($n) }
            }
        }
    }
}
function Redact([string]$t) {
    if ($NoRedact) { return $t }
    foreach ($n in ($names | Sort-Object Length -Descending)) { $t = $t.Replace($n, "<player$([array]::IndexOf($names.ToArray(), $n) + 1)>") }
    $t = [regex]::Replace($t, '(?i)([A-Z]:\\(Users|Documents and Settings)\\)[^\\/:*?"<>|]+', '$1<user>')
    if ($env:USERNAME -and $env:USERNAME.Length -ge 3) { $t = [regex]::Replace($t, '(?i)' + [regex]::Escape($env:USERNAME), '<user>') }
    if ($env:COMPUTERNAME) { $t = [regex]::Replace($t, '(?i)' + [regex]::Escape($env:COMPUTERNAME), '<pc>') }
    $t = [regex]::Replace($t, '\b7656119\d{10}\b', '<steamid>')
    $t = [regex]::Replace($t, '(?i)(lobby\s+)\d{6,}', '$1<lobby>')
    $t = [regex]::Replace($t, '((?i:room|code)[ =:]+''?)[A-Z0-9]{7}\b', '$1<code>')
    $t = [regex]::Replace($t, '\b(?!127\.)(?!0\.0\.0\.0\b)(\d{1,3}\.){3}\d{1,3}\b', '<ip>')
    $t = [regex]::Replace($t, '[\w.+-]+@[\w-]+\.[\w.]+', '<email>')
    return $t
}
function Add([string]$line) { $out.Add($line) }

Add "Crimson Desert Seamless Co-op+ diagnostics, $(Get-Date -Format s)"
Add "Windows: $([Environment]::OSVersion.VersionString)"
$pkg = Get-ChildItem $here -Filter 'README.txt' -ErrorAction SilentlyContinue | Select-Object -First 1
if ($pkg) { Add "Package: $((Get-Content $pkg.FullName -TotalCount 1).Trim())" }

if (-not $GameDir) { $GameDir = Find-CrimsonDesertBin64 }
Add "Game folder: $GameDir"
if ($GameDir -and (Test-Path (Join-Path $GameDir 'CrimsonDesert.exe'))) {
    Add "Game exe version: $((Get-Item (Join-Path $GameDir 'CrimsonDesert.exe')).VersionInfo.FileVersion)"
    Add "Game running now: $([bool](Get-Process -Name 'CrimsonDesert' -ErrorAction SilentlyContinue))"
    Add ''
    Add '--- mod files in bin64'
    foreach ($f in 'winmm.dll', 'cdmp.asi', 'cdmp.ini', 'coopplus.asi', 'coopplus.ini', 'coopplus-installed.txt', 'dinput8.dll', 'winhttp.dll', 'version.dll') {
        $p = Join-Path $GameDir $f
        if (Test-Path $p) {
            $i = Get-Item $p
            Add ("{0,-24} {1,10} bytes  {2}  sha256 {3}" -f $f, $i.Length, $i.LastWriteTime.ToString('s'),
                 (Get-FileHash $p -Algorithm SHA256).Hash.Substring(0, 16).ToLower())
        }
    }
    $asis = Get-ChildItem $GameDir -Filter '*.asi' -ErrorAction SilentlyContinue | Where-Object { $_.Name -ne 'cdmp.asi' -and $_.Name -ne 'coopplus.asi' }
    if ($asis) { Add ("Other ASI mods: " + (($asis | ForEach-Object Name) -join ', ')) }
    $ini = Join-Path $GameDir 'cdmp.ini'
    if (Test-Path $ini) {
        Add ''
        Add '--- cdmp.ini (settings only)'
        $bytes = [IO.File]::ReadAllBytes($ini)
        $text = if ($bytes.Length -ge 2 -and $bytes[0] -eq 0xFF -and $bytes[1] -eq 0xFE) {
            [Text.Encoding]::Unicode.GetString($bytes)
        } else { [Text.Encoding]::UTF8.GetString($bytes) }
        foreach ($l in ($text -split "`r?`n")) {
            $t = $l.Trim()
            if ($t -and -not $t.StartsWith(';')) { Add $t }
        }
    }
    # Shared world (2026-11-11): the Co-op+ layer (coopplus.asi) has its own settings and its own log; the menus, the
    # overlay and the lobby are in that log, so a report without it could not explain a missing Co-op menu.
    $ini2 = Join-Path $GameDir 'coopplus.ini'
    if (Test-Path $ini2) {
        Add ''
        Add '--- coopplus.ini (settings only)'
        $bytes = [IO.File]::ReadAllBytes($ini2)
        $text = if ($bytes.Length -ge 2 -and $bytes[0] -eq 0xFF -and $bytes[1] -eq 0xFE) {
            [Text.Encoding]::Unicode.GetString($bytes)
        } else { [Text.Encoding]::UTF8.GetString($bytes) }
        foreach ($l in ($text -split "`r?`n")) {
            $t = $l.Trim()
            if ($t -and -not $t.StartsWith(';')) { Add $t }
        }
    }
    $log2 = Join-Path $GameDir 'coopplus.log'
    if (Test-Path $log2) { Learn-Names (Get-Content $log2) }
    $log = Join-Path $GameDir 'cdmp.log'
    Add ''
    if (Test-Path $log) { Learn-Names (Get-Content $log) }
    if (Test-Path $ini) { Learn-Names ($out.ToArray()) }
    if (Test-Path $log) {
        Add "--- cdmp.log (last $LogLines lines; written $((Get-Item $log).LastWriteTime.ToString('s')))"
        Get-Content $log -Tail $LogLines | ForEach-Object { Add $_ }
    } else {
        Add '--- no cdmp.log: the mod has not run (is winmm.dll there? did the game start through Steam?)'
    }
    if (Test-Path (Join-Path $GameDir 'coopplus.asi')) {
        Add ''
        if (Test-Path $log2) {
            Add "--- coopplus.log (last $LogLines lines; written $((Get-Item $log2).LastWriteTime.ToString('s')))"
            Get-Content $log2 -Tail $LogLines | ForEach-Object { Add $_ }
        } else {
            Add '--- no coopplus.log: the Co-op+ layer has not run'
        }
    }
} else {
    Add 'Crimson Desert not found through Steam.'
}

$desktop = if ($OutDir) { $OutDir } else { [Environment]::GetFolderPath('Desktop') }
$file = Join-Path $desktop ("CoopPlus-report-{0}.txt" -f (Get-Date -Format 'yyyy-MM-dd_HH-mm-ss'))
$out | ForEach-Object { Redact $_ } | Set-Content -Path $file -Encoding UTF8
Write-Host "Report written: $file" -ForegroundColor Green
# Co-op+ (T37): the bundle: the report and the whole log (redacted), zipped, on the clipboard.
$zip = [IO.Path]::ChangeExtension($file, '.zip')
$tmp = Join-Path ([IO.Path]::GetTempPath()) ("coopplus-report-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force $tmp | Out-Null
Copy-Item $file (Join-Path $tmp 'report.txt')
if ($GameDir -and (Test-Path (Join-Path $GameDir 'cdmp.log'))) {
    Get-Content (Join-Path $GameDir 'cdmp.log') -Tail 50000 | ForEach-Object { Redact $_ } |
        Set-Content -Path (Join-Path $tmp 'cdmp.log') -Encoding UTF8
}
if ($GameDir -and (Test-Path (Join-Path $GameDir 'coopplus.log'))) {
    Get-Content (Join-Path $GameDir 'coopplus.log') -Tail 50000 | ForEach-Object { Redact $_ } |
        Set-Content -Path (Join-Path $tmp 'coopplus.log') -Encoding UTF8
}
Compress-Archive -Path (Join-Path $tmp '*') -DestinationPath $zip -Force
Remove-Item -Recurse -Force $tmp
Write-Host "Bundle written: $zip" -ForegroundColor Green
if (-not $NoClipboard) {
    try {
        Set-Clipboard -Path $zip
        Write-Host 'The bundle is on the clipboard: paste it (Ctrl+V) into a message or the Nexus bug report.' -ForegroundColor Green
    } catch { Write-Host 'Attach the .zip from your Desktop to your message.' }
}
if ($NoRedact) { Write-Host 'Not redacted (-NoRedact): it contains Steam names and ids.' -ForegroundColor Yellow }
else { Write-Host 'Steam names and ids, your Windows user and PC name and IP addresses were taken out.' }
Write-Host 'It reads the game folder and the mod log only.'
if (-not $NoPause) { Write-Host ''; Read-Host 'Press Enter to close' | Out-Null }
