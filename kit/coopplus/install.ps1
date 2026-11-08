# Crimson Desert Seamless Co-op+ installer.
# Started by "Install Co-op+.cmd". Safe to run again (it updates the files).
#
# What it does, in order, and it stops at the first problem:
#   1. finds the game through Steam (or uses -GameDir);
#   2. refuses while the game is running;
#   3. checks the game's EXE (version, build stamp, image size) and finds the ASI loader: an Ultimate ASI Loader
#      already in the game folder (winmm.dll, winhttp.dll, dinput8.dll, ...) is used as it is; the bundled winmm.dll
#      is installed only when there is none and winmm.dll is free (it stops when two loaders are found, or when
#      winmm.dll belongs to something else);
#   4. copies your solo AND co-op saves to Documents\CrimsonDesert-CoopPlus-SaveBackups\<date-time> and checks every
#      copied file against the original (SHA-256) before anything is installed;
#   5. keeps any cdmp.asi / cdmp.ini already in the game folder as *.before-coopplus;
#   6. copies the mod files into the game's bin64 folder and lists them in coopplus-installed.txt,
#      which "Uninstall Co-op+.cmd" uses to remove exactly those files.
# Nothing else on the PC is changed.

param(
    [string]$GameDir = '',     # the game's bin64 folder (found through Steam when empty)
    [string]$SaveDir = '',     # the save folder (default: %LOCALAPPDATA%\Pearl Abyss\CD\save)
    [string]$BackupRoot = '',  # where save backups go (default: Documents\CrimsonDesert-CoopPlus-SaveBackups)
    [switch]$NoPause
)
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$payload = Join-Path $here 'mod'
. (Join-Path $here 'coopplus-common.ps1')
$expectedExeVersion = '1.0.0.3576'
$expectedExeStamp = 0x6AED2E85   # the PE build stamp of 1.0.0.3576
$expectedExeImage = 0x13CC4000   # its SizeOfImage
$files = @('winmm.dll', 'cdmp.asi', 'cdmp.ini', 'cdcoop\config\scaling.toml')
# Files players edit: an update keeps theirs and puts the new defaults next to them.
$settingsFiles = @('cdmp.ini', 'cdcoop\config\scaling.toml')
# The shared-world package (B01, docs/design/native-join-and-saves.md): the native base is cdmp.asi, Co-op+ is
# coopplus.asi with the player's settings in coopplus.ini; cdmp.ini is then Co-op+'s own launch-mode file (solo /
# host / guest), never the player's: an update replaces it.
$native = Test-Path (Join-Path $payload 'coopplus.asi')
if ($native) {
    $files += 'coopplus.asi', 'coopplus.ini'
    $settingsFiles = @('coopplus.ini', 'cdcoop\config\scaling.toml')
}

function Done([int]$code) {
    if (-not $NoPause) { Write-Host ''; Read-Host 'Press Enter to close' | Out-Null }
    exit $code
}
function Fail([string]$msg) {
    Write-Host ''
    Write-Host "NOT INSTALLED: $msg" -ForegroundColor Red
    Done 1
}

Write-Host 'Crimson Desert Seamless Co-op+ - install' -ForegroundColor Cyan
Write-Host ''

foreach ($f in $files) {
    if (-not (Test-Path (Join-Path $payload $f))) { Fail "the package is incomplete ($f missing). Download it again." }
}

if (-not $GameDir) { $GameDir = Find-CrimsonDesertBin64 }
if (-not $GameDir -or -not (Test-Path (Join-Path $GameDir 'CrimsonDesert.exe'))) {
    Fail "Crimson Desert was not found through Steam. Run this again from PowerShell with -GameDir `"<your Steam library>\steamapps\common\Crimson Desert\bin64`"."
}
Write-Host "Game folder: $GameDir"

if (Get-Process -Name 'CrimsonDesert' -ErrorAction SilentlyContinue) {
    Fail 'Crimson Desert is running. Close the game first, then run the installer again.'
}

# The game's EXE: version, PE build stamp and image size of the build this mod was made for (B22a).
$exePath = Join-Path $GameDir 'CrimsonDesert.exe'
$exeVersion = (Get-Item $exePath).VersionInfo.FileVersion
$exeStamp = 0; $exeImage = 0
try {
    $fs = [IO.File]::OpenRead($exePath)
    try {
        $br = New-Object IO.BinaryReader($fs)
        if ($br.ReadUInt16() -eq 0x5A4D) {
            $fs.Position = 60; $pe = $br.ReadUInt32()
            if ($pe -lt 1048576) {
                $fs.Position = $pe
                if ($br.ReadUInt32() -eq 0x4550) {
                    $fs.Position = $pe + 8; $exeStamp = $br.ReadUInt32()
                    $fs.Position = $pe + 24 + 56; $exeImage = $br.ReadUInt32()
                }
            }
        }
    } finally { $fs.Dispose() }
} catch { }
if ($exeVersion -ne $expectedExeVersion -or $exeStamp -ne $expectedExeStamp -or $exeImage -ne $expectedExeImage) {
    Write-Host ("Note: your game is version {0} (build stamp {1:X8}, image {2:X}); this mod was built for {3} ({4:X8}, {5:X})." -f
        $exeVersion, $exeStamp, $exeImage, $expectedExeVersion, $expectedExeStamp, $expectedExeImage) -ForegroundColor Yellow
    Write-Host '      On another build the mod switches itself off and the game runs normally.' -ForegroundColor Yellow
}

# What this installer put here before (an update keeps the earlier list too, so Uninstall still removes everything
# we ever put there).
$listPath = Join-Path $GameDir 'coopplus-installed.txt'
$installed = @()
if (Test-Path $listPath) {
    $installed += Get-Content $listPath | Where-Object { $_ -and -not $_.StartsWith('#') -and $_ -ne 'coopplus-installed.txt' }
}

# The ASI loader (B22a, after Plitos's installer): an Ultimate ASI Loader already in the game folder, under any of the
# names it can take, is used as it is. The bundled winmm.dll goes in only when there is none and winmm.dll is free;
# a winmm.dll this installer put there earlier is still ours to update.
$proxyNames = @('winmm.dll', 'winhttp.dll', 'dinput8.dll', 'version.dll', 'dsound.dll', 'xinput1_3.dll', 'xinput1_4.dll',
    'd3d9.dll', 'd3d11.dll', 'wininet.dll', 'msacm32.dll')
$loaders = @(foreach ($name in $proxyNames) {
    $path = Join-Path $GameDir $name
    if (Test-Path -LiteralPath $path -PathType Leaf) {
        $text = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($path))
        if ($text.Contains('IsUltimateASILoader')) { $name }
    }
})
if ($loaders.Count -gt 1) {
    Fail ('more than one ASI loader is in the game folder ({0}). Keep one of them (move the others out of bin64), then run the installer again.' -f ($loaders -join ', '))
}
$ourWinmm = $installed -contains 'winmm.dll'
if ($loaders.Count -eq 1 -and -not ($loaders[0] -eq 'winmm.dll' -and $ourWinmm)) {
    $files = @($files | Where-Object { $_ -ne 'winmm.dll' })
    Write-Host "ASI loader: your existing $($loaders[0]) is used (the bundled winmm.dll is not installed)."
} elseif ($loaders.Count -eq 0 -and (Test-Path (Join-Path $GameDir 'winmm.dll')) -and -not $ourWinmm) {
    Fail 'winmm.dll in the game folder belongs to something else and is not an ASI loader, so the bundled loader cannot go there. Nothing was installed. If another mod needs that winmm.dll, add an Ultimate ASI Loader under another name (for example winhttp.dll) and run this again.'
} else {
    Write-Host 'ASI loader: the bundled winmm.dll.'
}

# Saves first, always: the solo saves and the co-op ones (the shared world's coop_worlds and the guest's own saves),
# each file checked against its copy before anything is installed.
if (-not $SaveDir) { $SaveDir = Join-Path $env:LOCALAPPDATA 'Pearl Abyss\CD\save' }
if (-not $BackupRoot) { $BackupRoot = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'CrimsonDesert-CoopPlus-SaveBackups' }
$saveRoot = Split-Path -Parent $SaveDir
$trees = @(@{ name = 'save'; path = $SaveDir }, @{ name = 'coop_worlds'; path = (Join-Path $saveRoot 'coop_worlds') },
           @{ name = 'save_cdmp_guest'; path = (Join-Path $saveRoot 'save_cdmp_guest') })
$dest = Join-Path $BackupRoot (Get-Date -Format 'yyyy-MM-dd_HH-mm-ss')
$total = 0
foreach ($tree in $trees) {
    if (-not (Test-Path -LiteralPath $tree.path -PathType Container)) { continue }
    $root = (Resolve-Path -LiteralPath $tree.path).Path.TrimEnd('\')
    $entries = @(Get-Item -LiteralPath $root -Force) + @(Get-ChildItem -LiteralPath $root -Force -Recurse)
    if ($entries | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }) {
        Fail "the save folder $root contains a link, so it was not backed up and nothing was installed. Move the linked folder out (or back it up yourself), then run the installer again."
    }
    $to = Join-Path $dest $tree.name
    New-Item -ItemType Directory -Force $to | Out-Null
    foreach ($file in $entries | Where-Object { -not $_.PSIsContainer }) {
        $copy = Join-Path $to $file.FullName.Substring($root.Length + 1)
        New-Item -ItemType Directory -Force (Split-Path -Parent $copy) | Out-Null
        Copy-Item -LiteralPath $file.FullName -Destination $copy -Force
        if ((Get-FileHash -LiteralPath $file.FullName).Hash -ne (Get-FileHash -LiteralPath $copy).Hash) {
            Fail "the save backup of $($file.FullName) did not match the original, so nothing was installed. Check that Documents has free space and is not read-only (OneDrive: let it finish syncing), then run the installer again."
        }
        $total++
    }
    Write-Host "Saves backed up ($($tree.name)), each file checked: $to" -ForegroundColor Green
}
if ($total -eq 0) { Write-Host "No saves found under $saveRoot (new install?); nothing to back up." -ForegroundColor Yellow }
else { Write-Host "Save backup complete: $total file(s), every one verified." -ForegroundColor Green }

# Keep whatever was there before, once (never overwrite an earlier keep).
# The shared-world package over an earlier Co-op+: that one's cdmp.ini held the player's settings; they move to
# coopplus.ini (once, before the copy below keeps them as the player's own).
if ($native) {
    $oldIni = Join-Path $GameDir 'cdmp.ini'
    $newIni = Join-Path $GameDir 'coopplus.ini'
    if ((Test-Path $oldIni) -and -not (Test-Path $newIni) -and (Test-Path $listPath)) {
        $oldText = [IO.File]::ReadAllText($oldIni)
        if ($oldText -notmatch 'coopplus_mode=') {
            Copy-Item $oldIni $newIni
            $installed += 'coopplus.ini'
            Write-Host 'Your Co-op+ settings moved from cdmp.ini to coopplus.ini'
        }
    }
}
# Vanilla mode (vanilla.ps1) parks the mod as cdmp.asi.off (and coopplus.asi.off); installing turns it back on.
$parkedLayer = Join-Path $GameDir 'coopplus.asi.off'
if (Test-Path $parkedLayer) { Remove-Item $parkedLayer -Force }
$parked = Join-Path $GameDir 'cdmp.asi.off'
if (Test-Path $parked) {
    Remove-Item $parked -Force
    Write-Host 'Vanilla mode was on; this install turns Co-op+ back on.'
}
foreach ($f in $files) {
    $target = Join-Path $GameDir $f
    $source = Join-Path $payload $f
    if (Test-Path $target) {
        $same = (Get-FileHash $target).Hash -eq (Get-FileHash $source).Hash
        $kept = "$target.before-coopplus"
        $ours = Test-Path (Join-Path $GameDir 'coopplus-installed.txt')
        if (-not $same -and -not $ours -and -not (Test-Path $kept)) {
            Copy-Item $target $kept
            Write-Host "Kept your existing $f as $f.before-coopplus"
        }
        if ($settingsFiles -contains $f -and $ours -and -not $same) {
            # An update keeps the player's own settings; the new defaults go next to them.
            Copy-Item $source "$target.new" -Force
            Write-Host "Kept your $f settings (new defaults saved as $f.new)"
            # 2026-11-07: the guest's stagger works on 3576 now. Earlier zips shipped guest_hit_stagger=0 under the
            # comment "0 is safer"; a player could not have chosen 0 deliberately under that text (it was the
            # default), so exactly that old shipped block is switched on, once. Any other value is left alone.
            if ($f -eq 'cdmp.ini' -or $f -eq 'coopplus.ini') {
                $text = [IO.File]::ReadAllText($target)
                $old = '(?m)^; The stagger reaction on those blows\. 0 is safer: it injects a hit reaction\r?\n; into the other player''s enemy, which its AI may not expect\.\r?\nguest_hit_stagger=0[ \t]*(?=\r?$)'
                if ($text -match $old) {
                    $text = [regex]::Replace($text, $old, "; The stagger reaction on those blows (1 = they stagger the enemy as your own blows do).`r`n; 0 = damage only, no hit reaction.`r`nguest_hit_stagger=1")
                    [IO.File]::WriteAllText($target, $text, (New-Object Text.UnicodeEncoding($false, $true)))
                    Write-Host ('Turned on: the guest''s blows now stagger enemies (guest_hit_stagger=1 in ' + $f + ')')
                }
            }
            $installed += $f, "$f.new"
            continue
        }
    }
    New-Item -ItemType Directory -Force (Split-Path -Parent $target) | Out-Null
    Copy-Item $source $target -Force
    $installed += $f
}
$installed = @($installed | Select-Object -Unique) + 'coopplus-installed.txt'
Set-Content -Path $listPath -Encoding ascii -Value (
    @("# Crimson Desert Seamless Co-op+ installed $(Get-Date -Format s). Files below are removed by Uninstall.") + $installed)

Write-Host ''
Write-Host 'Installed. Start Crimson Desert from Steam as usual.' -ForegroundColor Green
Write-Host 'In game: F8 shows the co-op panel, Ctrl+F1 invites a Steam friend.'
Write-Host 'Your friend accepts the invite (or uses "Join Game" on you in Steam) and lands in your world.'
Done 0
