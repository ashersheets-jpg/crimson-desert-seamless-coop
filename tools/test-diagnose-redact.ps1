# T37: the Diagnose bundle on a fake game folder with a log full of personal data. Everything personal must be gone
# from the .txt and the zip's cdmp.log; the game version and coordinates must stay. Exit 1 on a leak.
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$t = Join-Path ([IO.Path]::GetTempPath()) ("coopplus-diag-" + (Get-Date -Format 'HHmmss'))
$bin = Join-Path $t 'Crimson Desert\bin64'
New-Item -ItemType Directory -Force $bin | Out-Null
Copy-Item "$env:WINDIR\System32\where.exe" (Join-Path $bin 'CrimsonDesert.exe')
$secrets = @('Kliffhanger_77', 'Oongka Fan', '76561198012345678', '109775241234567890', 'QX7P2KD', "$env:USERNAME", "$env:COMPUTERNAME", '192.168.1.44', 'someone@example.com')
@(
    "[16:00:00.000] INF [T1] Host process: C:\Users\$env:USERNAME\Games\CrimsonDesert.exe (PID 1)",
    "[16:00:00.000] INF [T1] Host version: 1.0.0.3479  image base: 0x140000000",
    "[16:00:00.000] INF [T1] Co-op+: playing as 'Kliffhanger_77' (Steam name)",
    "[16:00:01.000] INF [T1] Room: hosting, code QX7P2KD (lobby 109775241234567890, host SteamID64 76561198012345678)",
    "[16:00:02.000] INF [T1] Co-op: connected to 'Oongka Fan'",
    "[16:00:03.000] INF [T1] Local: our horse at (-10300.12, 535.40, -3915.00) on $env:COMPUTERNAME",
    "[16:00:04.000] INF [T1] Network: udp 192.168.1.44:37015 and 127.0.0.1:37015; mail someone@example.com",
    "[16:00:05.000] WRN [T1] Save guard: the game saved (slot 2) while 'Oongka Fan' is in the session [1 so far]"
) | Set-Content (Join-Path $bin 'cdmp.log') -Encoding UTF8
# Earlier runs' logs (the title part of a session is in them): the two newest of each kind go in the zip, redacted
# with the names learned from ALL logs ('Title Tester' appears only in an earlier run's log).
Copy-Item (Join-Path $bin 'cdmp.log') (Join-Path $bin 'cdmp_20261112-173612.log')   # the current run's dated name
@("[15:00:00.000] INF [T1] Co-op+: playing as 'Title Tester' (Steam name)",
  "[15:00:01.000] INF [T1] Room: hosting, code QX7P2KD (lobby 109775241234567890, host SteamID64 76561198012345678) PREVBASE1"
) | Set-Content (Join-Path $bin 'cdmp_20261112-173000.log') -Encoding UTF8
"[14:00:00.000] INF [T1] udp 192.168.1.44:37015 for Title Tester PREVBASE2" | Set-Content (Join-Path $bin 'cdmp_20261112-172000.log') -Encoding UTF8
"[13:00:00.000] INF [T1] too old PREVBASE3 Kliffhanger_77" | Set-Content (Join-Path $bin 'cdmp_20261112-171000.log') -Encoding UTF8
"[13:00:00.000] INF [T1] not a log of ours Kliffhanger_77" | Set-Content (Join-Path $bin 'cdmp_packets_20261112-171000.log') -Encoding UTF8
"[15:00:02.000] INF [T1] Co-op lobby: Title Tester invited Oongka Fan PREVLAYER1 someone@example.com" | Set-Content (Join-Path $bin 'coopplus.1.log') -Encoding UTF8
"[14:00:02.000] INF [T1] Co-op: connected to 'Oongka Fan' PREVLAYER2" | Set-Content (Join-Path $bin 'coopplus.2.log') -Encoding UTF8
"[13:00:02.000] INF [T1] too old PREVLAYER3" | Set-Content (Join-Path $bin 'coopplus.3.log') -Encoding UTF8
$secrets += 'Title Tester'
& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root 'kit\coopplus\diagnose.ps1') -GameDir $bin -OutDir $t -NoClipboard -NoPause | Out-Null
$txt = Get-ChildItem $t -Filter 'CoopPlus-report-*.txt' | Select-Object -First 1
$zip = Get-ChildItem $t -Filter 'CoopPlus-report-*.zip' | Select-Object -First 1
Expand-Archive $zip.FullName (Join-Path $t 'unzipped')
$all = (Get-Content $txt.FullName -Raw) + ((Get-ChildItem (Join-Path $t 'unzipped') -File | ForEach-Object { Get-Content $_.FullName -Raw }) -join "`n")
$fails = 0
$packed = (Get-ChildItem (Join-Path $t 'unzipped') -File | Sort-Object Name | ForEach-Object Name) -join ' '
$want = 'cdmp.log cdmp.prev1.log cdmp.prev2.log coopplus.prev1.log coopplus.prev2.log report.txt'
if ($packed -eq $want) { Write-Host "files $packed" } else { Write-Host "FILES $packed (wanted $want)" -ForegroundColor Red; $fails++ }
foreach ($m in 'PREVBASE1', 'PREVBASE2', 'PREVLAYER1', 'PREVLAYER2') {
    if ($all.Contains($m)) { Write-Host "packed $m" } else { Write-Host "MISSING $m" -ForegroundColor Red; $fails++ }
}
foreach ($m in 'PREVBASE3', 'PREVLAYER3', 'not a log of ours') {
    if ($all.Contains($m)) { Write-Host "PACKED BUT SHOULD NOT BE: $m" -ForegroundColor Red; $fails++ } else { Write-Host "left out $m" }
}
foreach ($s in $secrets) {
    if ($s -and $all.Contains($s)) { Write-Host "LEAK  $s" -ForegroundColor Red; $fails++ } else { Write-Host "gone  $s" }
}
foreach ($keep in '1.0.0.3479', '-10300.12', '127.0.0.1') {
    if ($all.Contains($keep)) { Write-Host "kept  $keep" } else { Write-Host "LOST  $keep" -ForegroundColor Red; $fails++ }
}
Write-Host '--- the redacted log:'
Get-Content (Join-Path $t 'unzipped\cdmp.log')
Remove-Item -Recurse -Force $t
if ($fails) { exit 1 }
