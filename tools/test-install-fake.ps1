# T27: clean-machine walkthrough of the release zips on a FAKE game folder (never the real one).
#
#   .\tools\test-install-fake.ps1 [-Zip dist\...zip]   (default: the newest player zip in dist)
#
# A temp folder gets "Crimson Desert\bin64" with a stand-in CrimsonDesert.exe, plus a stand-in solo save and a
# stand-in co-op world. Then, as a player would: unzip, install, play-edit the settings, install again (update),
# vanilla on and off, uninstall; and the mod-manager zip unpacked by hand. After the uninstall the folder must be
# byte-for-byte what it was before. B22a (2026-11-08): the ASI loader cases on their own fake folders (an existing
# loader under winhttp.dll is reused; a winmm.dll that is not a loader, or two loaders, stop the install with
# nothing changed) and the verified backup of solo AND co-op saves. Prints PASS/FAIL per step; exit 1 on any FAIL.
# -RepoScripts: the repo's kit\coopplus scripts replace the zip's (after the zip's own SHA256 check), to test them
# before a package is made.
param([string]$Zip = '', [switch]$RepoScripts)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (-not $Zip) {
    $Zip = (Get-ChildItem (Join-Path $root 'dist') -Filter 'CrimsonDesert-SeamlessCoop-*.zip' |
        Where-Object { $_.Name -notmatch 'symbols|mod-manager' } | Sort-Object LastWriteTime | Select-Object -Last 1).FullName
}
$managerZip = $Zip -replace '\.zip$', '-mod-manager.zip'
$t = Join-Path ([IO.Path]::GetTempPath()) ("coopplus-fake-" + (Get-Date -Format 'HHmmss'))
$game = Join-Path $t 'Crimson Desert'; $bin = Join-Path $game 'bin64'
$saves = Join-Path $t 'save'; $backups = Join-Path $t 'backups'; $kit = Join-Path $t 'download'
New-Item -ItemType Directory -Force $bin, $saves | Out-Null
Copy-Item "$env:WINDIR\System32\where.exe" (Join-Path $bin 'CrimsonDesert.exe')  # any exe: the version note is expected
Set-Content (Join-Path $bin 'other-mod.txt') 'a file of another mod'
Set-Content (Join-Path $saves 'slot0.save') 'stand-in save'
New-Item -ItemType Directory -Force (Join-Path $t 'coop_worlds\with_friend') | Out-Null
Set-Content (Join-Path $t 'coop_worlds\with_friend\slot100.save') 'stand-in co-op save'
$fails = 0
function Check([string]$what, [bool]$ok) {
    if ($ok) { Write-Host "PASS  $what" -ForegroundColor Green } else { Write-Host "FAIL  $what" -ForegroundColor Red; $script:fails++ }
}
function Snapshot([string]$dir) {
    Get-ChildItem $dir -Recurse -File | ForEach-Object { '{0} {1}' -f $_.FullName.Substring($dir.Length), (Get-FileHash $_.FullName).Hash } | Sort-Object
}
$before = Snapshot $bin
Write-Host "Fake game: $game"
Write-Host "Zip: $Zip"

Expand-Archive $Zip $kit
$present = @('README.txt', 'BUG-REPORT.txt', 'OTHER-MODS.md', 'CREDITS.md', 'LICENSE.txt', 'source.zip', 'SHA256.txt', 'Install Co-op+.cmd', 'Uninstall Co-op+.cmd') |
    ForEach-Object { Test-Path (Join-Path $kit $_) }
Check 'zip has README, BUG-REPORT, OTHER-MODS, CREDITS, LICENSE, source.zip, SHA256' ($present -notcontains $false)
$bad = Get-Content (Join-Path $kit 'SHA256.txt') | Where-Object { $_ } | Where-Object {
    $h, $f = $_ -split '  ', 2; (Get-FileHash (Join-Path $kit $f)).Hash.ToLower() -ne $h }
Check 'SHA256.txt matches every file' (-not $bad)
if ($RepoScripts) {
    Get-ChildItem (Join-Path $root 'kit\coopplus') -Filter '*.ps1' | ForEach-Object { Copy-Item $_.FullName (Join-Path $kit $_.Name) -Force }
    Write-Host 'Using the repo''s kit\coopplus scripts'
}

$o = & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $kit 'install.ps1') -GameDir $bin -SaveDir $saves -BackupRoot $backups -NoPause 2>&1 | Out-String
Check 'install: says Installed' ($o -match 'Installed\.')
Check 'install: mod files in bin64' ((@('winmm.dll', 'cdmp.asi', 'cdmp.ini', 'cdcoop\config\scaling.toml') | ForEach-Object { Test-Path (Join-Path $bin $_) }) -notcontains $false)
Check 'install: the bundled winmm.dll (no loader was there)' (($o -match 'ASI loader: the bundled winmm.dll') -and
    (Get-FileHash (Join-Path $bin 'winmm.dll')).Hash -eq (Get-FileHash (Join-Path $kit 'mod\winmm.dll')).Hash)
Check 'install: solo AND co-op saves backed up, each verified' (((Get-ChildItem $backups -Recurse -File | Measure-Object).Count -eq 2) -and
    (@(Get-ChildItem $backups -Recurse -File -Filter 'slot100.save').Count -eq 1) -and ($o -match 'every one verified'))
Check 'install: version note for another game version' ($o -match 'this mod was built for')

# A player edits the settings; an update must keep them.
# The shared-world zip (coopplus.asi): the player's settings are coopplus.ini (cdmp.ini is Co-op+'s launch file).
$native = Test-Path (Join-Path $kit 'mod\coopplus.asi')
if ($native) {
    Check 'install (shared world): coopplus.asi, coopplus.ini and a Solo cdmp.ini' ((Test-Path (Join-Path $bin 'coopplus.asi')) -and
        (Test-Path (Join-Path $bin 'coopplus.ini')) -and ([IO.File]::ReadAllText((Join-Path $bin 'cdmp.ini'), [Text.Encoding]::Unicode) -match 'coopplus_mode=solo'))
}
$ini = Join-Path $bin $(if ($native) { 'coopplus.ini' } else { 'cdmp.ini' })
$text = [IO.File]::ReadAllText($ini, [Text.Encoding]::Unicode) -replace 'join=friends', 'join=invite'
# ... and it came from an older zip, which shipped the guest's stagger off under the old comment (the update turns it on).
$text = [regex]::Replace($text, '(?m)^; The stagger reaction on those blows.*\r?\n; 0 = damage only.*\r?\nguest_hit_stagger=1',
    "; The stagger reaction on those blows. 0 is safer: it injects a hit reaction`r`n; into the other player's enemy, which its AI may not expect.`r`nguest_hit_stagger=0")
if ($text -notmatch 'guest_hit_stagger=0') { Check 'update: an older ini made for the stagger test' $false }
[IO.File]::WriteAllText($ini, $text, (New-Object Text.UnicodeEncoding($false, $true)))
$o = & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $kit 'install.ps1') -GameDir $bin -SaveDir $saves -BackupRoot $backups -NoPause 2>&1 | Out-String
Check 'update: player settings kept, new defaults as <settings>.new' (([IO.File]::ReadAllText($ini, [Text.Encoding]::Unicode) -match 'join=invite') -and (Test-Path "$ini.new"))
Check 'update: an older zip''s guest_hit_stagger=0 turned on, once' (([IO.File]::ReadAllText($ini, [Text.Encoding]::Unicode) -match '(?m)^guest_hit_stagger=1') -and ($o -match 'Turned on: the guest'))

& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $kit 'vanilla.ps1') -GameDir $bin -Mode on -NoPause | Out-Null
Check 'vanilla on: cdmp.asi parked' ((Test-Path (Join-Path $bin 'cdmp.asi.off')) -and -not (Test-Path (Join-Path $bin 'cdmp.asi')))
if ($native) { Check 'vanilla on: coopplus.asi parked too' ((Test-Path (Join-Path $bin 'coopplus.asi.off')) -and -not (Test-Path (Join-Path $bin 'coopplus.asi'))) }
& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $kit 'vanilla.ps1') -GameDir $bin -Mode off -NoPause | Out-Null
Check 'vanilla off: cdmp.asi back' (Test-Path (Join-Path $bin 'cdmp.asi'))

Set-Content (Join-Path $bin 'cdmp.log') 'a log the mod wrote'
if ($native) { Set-Content (Join-Path $bin 'coopplus.log') 'a log the layer wrote' }
& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $kit 'uninstall.ps1') -GameDir $bin -NoPause | Out-Null
$after = Snapshot $bin
Check 'uninstall: bin64 exactly as before the install' (-not (Compare-Object $before $after))
Check 'uninstall: save backups left alone' ((Get-ChildItem $backups -Recurse -File | Measure-Object).Count -ge 1)

# B22a: the ASI loader cases, each on its own fake bin64 (the same zip, the same scripts).
function Fake-Bin([string]$name) {
    $b = Join-Path $t "$name\bin64"
    New-Item -ItemType Directory -Force $b | Out-Null
    Copy-Item "$env:WINDIR\System32\where.exe" (Join-Path $b 'CrimsonDesert.exe')
    return $b
}
$loaderText = 'MZ stand-in Ultimate ASI Loader IsUltimateASILoader'
# An existing loader under another name: used as it is, the bundled winmm.dll not installed, uninstall leaves it.
$b2 = Fake-Bin 'reuse'
Set-Content (Join-Path $b2 'winhttp.dll') $loaderText
$snap2 = Snapshot $b2
$o = & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $kit 'install.ps1') -GameDir $b2 -SaveDir $saves -BackupRoot $backups -NoPause 2>&1 | Out-String
Check 'loader: an existing winhttp.dll loader is used, no winmm.dll added' (($o -match 'Installed\.') -and ($o -match 'existing winhttp.dll') -and
    -not (Test-Path (Join-Path $b2 'winmm.dll')) -and (Test-Path (Join-Path $b2 'cdmp.asi')))
& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $kit 'uninstall.ps1') -GameDir $b2 -NoPause | Out-Null
Check 'loader: uninstall leaves the other loader and the folder as before' (-not (Compare-Object $snap2 (Snapshot $b2)))
# A winmm.dll that is not a loader: refused, nothing changed.
$b3 = Fake-Bin 'occupied'
Set-Content (Join-Path $b3 'winmm.dll') 'a proxy dll of another mod, not an ASI loader'
$snap3 = Snapshot $b3
$o = & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $kit 'install.ps1') -GameDir $b3 -SaveDir $saves -BackupRoot $backups -NoPause 2>&1 | Out-String
Check 'loader: a foreign winmm.dll stops the install, nothing changed' (($o -match 'NOT INSTALLED') -and ($o -match 'belongs to something else') -and
    -not (Compare-Object $snap3 (Snapshot $b3)))
# Two loaders: refused, nothing changed.
$b4 = Fake-Bin 'two'
Set-Content (Join-Path $b4 'winmm.dll') $loaderText
Set-Content (Join-Path $b4 'dinput8.dll') $loaderText
$snap4 = Snapshot $b4
$o = & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $kit 'install.ps1') -GameDir $b4 -SaveDir $saves -BackupRoot $backups -NoPause 2>&1 | Out-String
Check 'loader: two loaders stop the install, nothing changed' (($o -match 'NOT INSTALLED') -and ($o -match 'more than one ASI loader') -and
    -not (Compare-Object $snap4 (Snapshot $b4)))

if (Test-Path $managerZip) {
    Expand-Archive $managerZip $game -Force
    Check 'mod-manager zip: cdmp.asi and winmm.dll land next to CrimsonDesert.exe' (
        (Test-Path (Join-Path $bin 'cdmp.asi')) -and (Test-Path (Join-Path $bin 'cdcoop\config\scaling.toml')) -and
        (Get-FileHash (Join-Path $bin 'cdmp.asi')).Hash -eq (Get-FileHash (Join-Path $kit 'mod\cdmp.asi')).Hash)
} else {
    Check "mod-manager zip present ($managerZip)" $false
}
Remove-Item -Recurse -Force $t
Write-Host ''
if ($fails) { Write-Host "$fails step(s) FAILED" -ForegroundColor Red; exit 1 }
Write-Host 'All steps passed' -ForegroundColor Green
