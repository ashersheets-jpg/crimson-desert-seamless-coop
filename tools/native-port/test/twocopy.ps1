# Native base on 1.0.0.3549: two copies on this PC, host + guest (ByJunter's option D), first join.
# Called by twocopy.sh (test_guard.sh first, cleanup after). Saves:
#  - host: everything redirected into %LOCALAPPDATA%\Pearl Abyss\CD\cdmp_save_sandbox (a copy made at start), the
#    test slot and a windowed settings file copied into the sandbox only (CDMP_SANDBOX_ADD);
#  - guest: the guest role sends the whole save folder to ...\CD\save_cdmp_guest, which this script fills with a test
#    slot and a windowed settings file. The real save folder is only read (its settings file, as the template).
param([string]$Name = 'tc1', [string]$HostVariant = 'coop-test-bandits', [string]$GuestVariant = 'rps',
      [int]$LoadWait = 80, [int]$JoinWait = 120, [int]$Hold = 60, [switch]$Walk, [switch]$Ride, [switch]$Fight, [switch]$CoopPlus, [switch]$Kill,
      # B01 co-op saves (Co-op+ docs/design/native-join-and-saves.md): [session2] coop_save_dir / coop_save_seed per role
      [string]$HostCoop = '', [string]$HostSeed = 'keep', [string]$GuestCoop = '', [string]$GuestSeed = 'keep',
      # B01 join flow (-Lobby, needs -CoopPlus): both start SOLO (native off); the host's Co-op+ menu starts co-op,
      # both games restart into the native modes (the layer writes cdmp.<role>.ini), then start together.
      [switch]$Lobby,
      # A Co-op+ dev script per role ([dev] script=..., run with Ctrl+Shift+F6 after the join), e.g. a teleport
      [string]$HostScript = '', [string]$GuestScript = '',
      # actpid steps for the guest after the scripts (e.g. 'kd:CTRL k:F4:120 ku:CTRL w:20000': go to the friend)
      [string]$GuestKeys = '',
      # actpid steps for the host after the guest's (joined with _ like -GuestKeys)
      [string]$HostKeys = '',
      # 'x_z': the guest walks there after the scripts and keys (camwalk\walkto.py, 3 m short)
      [string]$GuestWalkTo = '')
$ErrorActionPreference = 'Stop'
$bin = 'D:\SteamLibrary\steamapps\common\Crimson Desert\bin64'
$here = $PSScriptRoot; $saves = 'C:\Users\asher\CrimsonDesert-SeamlessCoop\build\test-saves'
$shot = 'C:\Users\asher\CrimsonDesert-SeamlessCoop\build\port3460\shot.ps1'
$cd = "$env:LOCALAPPDATA\Pearl Abyss\CD"; $acct = '1160582305'
if (Get-Process CrimsonDesert -ErrorAction SilentlyContinue) { throw 'Crimson Desert is running: test not started' }
if (-not (Get-Process steam -ErrorAction SilentlyContinue)) { throw 'Steam is not running' }
Copy-Item 'C:\Users\asher\CrimsonDesert-SeamlessCoop\upstream\cdmp-native\build\bin\RelWithDebInfo\cdmp.asi' "$bin\cdmp.asi" -Force

function Write-Ini([string]$path, [string]$text) {
    [IO.File]::WriteAllText($path, $text.Replace("`r`n", "`n").Replace("`n", "`r`n"), (New-Object Text.UnicodeEncoding($false, $true)))
}
Write-Ini "$bin\cdmp.host.ini" @'
[general]
player_name=Host
background_full_speed=1
[net]
enabled=1
transport=udp
role=host
peer_port=37015
host_loopback_only=1
[overlay]
enabled=0
[sniffer]
enabled=1
record=1
[coop]
enabled=0
[session2]
enabled=1
probe=1
save_guard=redirect
host_save=sandbox
create_session=1
accept_tunnel=1
share_field=1
relay_fix=1
clock_gate=1
identity_fix=1
child_hold=1
observer_hold=1
observer_load_defer=1
observer_hold_family=1
observer_hold_acks=1
character_hold=1
child_settle_ms=4000
child_reannounce=1
observer_seq_copy=1
observer_seq_copy_2322=1
observer_milestone=1
observer_milestone_send=1
observer_milestone_reload=1
observer_milestone_reload_any=1
guest_own_save=1
ui_pause_block=1
remote_interp=1
mount_sync=1
mount_seat=1
mount_relink=1
ride_filter=1
ride_dismount=native
remote_climb=1
wallup_send=1
dismount_probe=1
horse_follow=1
horse_follow_reverse=1
user2_fast_travel=1
'@
if ($HostCoop) { [IO.File]::AppendAllText("$bin\cdmp.host.ini", "`r`ncoop_save_dir=$HostCoop`r`ncoop_save_seed=$HostSeed`r`n", (New-Object Text.UnicodeEncoding($false, $false))); "host co-op save: $HostCoop ($HostSeed)" }
Write-Ini "$bin\cdmp.guest.ini" @'
[general]
player_name=Guest
background_full_speed=1
[net]
enabled=1
transport=udp
role=guest
peer_host=127.0.0.1
peer_port=37015
[overlay]
enabled=0
[sniffer]
enabled=1
record=1
[coop]
enabled=0
[session2]
enabled=1
guest=1
save_guard=redirect
guest_mode=remote
guest_crash_dump=1
guest_layer_guard=1
guest_remote_update_guard=1
guest_update_guard=1
guest_actor_diag=1
guest_reload_wait_fix=1
guest_own_save=1
guest_seq_route=1
ui_pause_block=1
remote_interp=1
mount_sync=1
mount_seat=1
mount_relink=1
ride_filter=1
ride_dismount=native
remote_climb=1
wallup_send=1
dismount_probe=1
guest_fast_travel_veto=probe
'@
if ($GuestCoop) { [IO.File]::AppendAllText("$bin\cdmp.guest.ini", "`r`ncoop_save_dir=$GuestCoop`r`ncoop_save_seed=$GuestSeed`r`n", (New-Object Text.UnicodeEncoding($false, $false))); "guest co-op save: $GuestCoop ($GuestSeed)" }

if ($CoopPlus) {
    # The Co-op+ layer (docs/research/native-coopplus-layer.md): coopplus.asi next to the native cdmp.asi, its
    # per-role inis (CDMP_ROLE picks coopplus.<role>.ini); its host listens on udp 37115 (the native tunnel uses
    # 37015). twocopy.sh removes the DLL and the inis afterwards.
    $layer = 'C:\Users\asher\CrimsonDesert-SeamlessCoop\upstream\cd-coop-3446\build-native-layer\bin\RelWithDebInfo\coopplus.asi'
    Copy-Item $layer "$bin\coopplus.asi" -Force
    foreach ($r in 'host', 'guest') {
        # NOT $name: PowerShell variables ignore case, $name would overwrite the test's $Name (tc34).
        $who = $(if ($r -eq 'host') { 'Host' } else { 'Guest' })
        # -Kill (tc48+): the host's layer gets the one-PC death test's script (KillCheatReq on its own key 0xA0100001),
        # sent on Ctrl+Shift+F6 through the injection bridge, so the revive step does not depend on the bandits.
        $dev = $(if ($Kill -and $r -eq 'host') { "[dev]`nscript=C:\Users\asher\CrimsonDesert-SeamlessCoop\build\port3479\scripts\selfkill.txt" } else { '' })
        $own = $(if ($r -eq 'host') { $HostScript } else { $GuestScript })
        if ($own) { $dev = "[dev]`nscript=$own" }
        Write-Ini "$bin\coopplus.$r.ini" @"
[general]
player_name=$who
onboarded=1
experimental_asked=1
[net]
enabled=1
transport=udp
role=$r
peer_host=127.0.0.1
peer_port=37115
[coop]
enabled=1
[overlay]
enabled=1
$dev
"@
    }
    # No relay: Co-op+'s host binds peer_port itself and its guest connects to it (tc34: a relay on the same port kept
    # the host from binding until the relay quit).
    "co-op+ layer: coopplus.asi, host on udp 37115"
}

# The windowed settings (template: the player's own file, read only).
$xml = [IO.File]::ReadAllText("$cd\save\user_engine_option_save.xml")
$win = $xml -replace '(<EnumSelectDisplayType Name="_displayType" _select=")[^"]*"', '$1Windowed"'
if ($win -eq $xml -and $xml -notmatch '_select="Windowed"') { throw 'display type not found in the settings file' }
# Two copies on one GPU: the lowest quality in both test copies (the player's own settings file is never written).
$win = $win -replace '(<EnumSelectQualityLevel Name="\w+" _select=")[^"]*"', '$1Minimum"'
$win = $win -replace '(<OptionStringVector Name="_postProcessingQualityLevelSelect" _value=")[^"]*"', '$1Minimum"'
$win = $win -replace '(<EnumSelectResolutionScale Name="_upscaleResolution" _select=")[^"]*"', '$1 50%"' -replace '_select=" 50%"', '_select="50%"'
[IO.File]::WriteAllText("$here\settings-windowed.xml", $win)

# The guest's own save folder (only ever made by this script; marked).
$g = "$cd\save_cdmp_guest"
if ((Test-Path $g) -and -not (Test-Path "$g\.coopplus-test")) { throw "$g exists and is not ours: left alone, test not started" }
if (Test-Path $g) { Remove-Item $g -Recurse -Force }
New-Item -ItemType Directory "$g\$acct" -Force | Out-Null
Set-Content "$g\.coopplus-test" 'made by build/native/twocopy.ps1'
Copy-Item "$saves\slot103-$GuestVariant" "$g\$acct\slot103" -Recurse
Copy-Item "$here\settings-windowed.xml" "$g\user_engine_option_save.xml"
Copy-Item "$cd\save\initialize_input.save" "$g\initialize_input.save"

Remove-Item "$bin\cdmp.host.log", "$bin\cdmp.guest.log" -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force "$here\shots" | Out-Null
$env:SteamAppId = '3321460'; $env:SteamGameId = '3321460'
function Start-Copy([string]$role, [string]$add) {
    $env:CDMP_ROLE = $role; $env:CDMP_SANDBOX_ADD = $add
    $env:CDMP_SANDBOX_ONLY = $(if ($role -eq 'host') { "$acct\slot103" } else { '' })
    $p = Start-Process "$bin\CrimsonDesert.exe" -ArgumentList 'PlatformServiceType=Steam' -WorkingDirectory $bin -PassThru
    Add-Content "$here\$Name.pids" $p.Id
    "$role pid=$($p.Id)"
    $t0 = Get-Date
    while (((Get-Date) - $t0).TotalSeconds -lt 150) {
        Start-Sleep 2
        if ($p.HasExited) { "$role exited early (code $($p.ExitCode))"; exit 2 }
        if ((Test-Path "$bin\cdmp.$role.log") -and (Select-String -Path "$bin\cdmp.$role.log" -Pattern 'Late init: game is up' -Quiet)) { break }
    }
    Start-Sleep 20
    return $p
}
if ($Lobby) {
    # Solo inis (no Co-op+ mark: the layer keeps them once as .before-coopplus and writes its own on Start Co-op).
    foreach ($r in 'host', 'guest') {
        $nm = $(if ($r -eq 'host') { 'Host' } else { 'Guest' })
        Write-Ini "$bin\cdmp.$r.ini" "[general]`nplayer_name=$nm`nbackground_full_speed=1`n[net]`nenabled=0`n[overlay]`nenabled=0`n[coop]`nenabled=0`n[session2]`nenabled=0`n"
        Remove-Item "$bin\cdmp.$r.ini.before-coopplus", "$bin\coopplus.$r.1.log" -ErrorAction SilentlyContinue
    }
    "lobby flow: both start solo"
}
Remove-Item "$here\$Name.pids" -ErrorAction SilentlyContinue
$h = Start-Copy 'host' "$saves\slot103-$HostVariant|$acct\slot103;$here\settings-windowed.xml|user_engine_option_save.xml"
$h = $h | Where-Object { $_ -is [System.Diagnostics.Process] }
& $shot -Out "$here\shots\$Name-00-host-title.png" | Out-Null
if ($Lobby) {
    $gp = Start-Copy 'guest' ''
    $gp = $gp | Where-Object { $_ -is [System.Diagnostics.Process] }
    Start-Sleep 15
    & $shot -Out "$here\shots\$Name-L0-both-titles.png" | Out-Null
    "host: Start Co-op, then the first enabled choice (Start From My Solo Progress) at $(Get-Date -Format HH:mm:ss.fff)"
    & "$here\actpid.ps1" -GamePid $h.Id -Seq "k:S:80 w:500 k:S:80 w:500 k:S:80 w:700 shot:$Name-L1-host-start-coop k:SPACE:120 w:1500 shot:$Name-L2-host-question k:SPACE:120 w:1000" | Out-Null
    # The host restarts, then the guest: wait for both new processes and the guest's connection to the host's world.
    $t0 = Get-Date; $remote = $false
    while (((Get-Date) - $t0).TotalSeconds -lt 420) {
        Start-Sleep 5
        if ((Test-Path "$bin\cdmp.guest.log") -and (Select-String -Path "$bin\cdmp.guest.log" -Pattern '-> REMOTE' -Quiet)) { $remote = $true; break }
    }
    "guest REMOTE: $remote at $(Get-Date -Format HH:mm:ss.fff)"
    Get-Process CrimsonDesert -ErrorAction SilentlyContinue | ForEach-Object { "running: pid $($_.Id) started $($_.StartTime.ToString('HH:mm:ss'))" }
    & $shot -Out "$here\shots\$Name-L3-after-restarts.png" | Out-Null
    # The host's menu goes on by itself (both in co-op): the countdown and Play on both. Each game presses Play only
    # with its window in front (a player's is): the host's window first, then the guest's (the restarted copies: the
    # host restarted first).
    $t0 = Get-Date
    while (((Get-Date) - $t0).TotalSeconds -lt 120) {
        Start-Sleep 2
        if (Select-String -Path "$bin\coopplus.host.log" -Pattern 'both games start in' -Quiet) { break }
    }
    $new = @(Get-Process CrimsonDesert -ErrorAction SilentlyContinue | Sort-Object StartTime | Select-Object -Last 2)
    if ($new.Count -eq 2) {
        "countdown seen; host pid $($new[0].Id), guest pid $($new[1].Id): each window in front for its Play"
        & "$here\actpid.ps1" -GamePid $new[0].Id -Seq "w:9000 shot:$Name-L3b-host-play" | Out-Null
        & "$here\actpid.ps1" -GamePid $new[1].Id -Seq "w:9000 shot:$Name-L3c-guest-play" | Out-Null
    }
    Start-Sleep $JoinWait
    & $shot -Out "$here\shots\$Name-L4-later.png" | Out-Null
    Start-Sleep 30
    & $shot -Out "$here\shots\$Name-L5-later.png" | Out-Null
    foreach ($r in 'host', 'guest') {
        Copy-Item "$bin\coopplus.$r.1.log" "$here\$Name.coopplus.$r.before.log" -ErrorAction SilentlyContinue
        Copy-Item "$bin\cdmp.$r.ini" "$here\$Name.cdmp.$r.ini" -ErrorAction SilentlyContinue
    }
    "lobby flow done"
    exit 0
}
# tcB2: with -CoopPlus the layer's co-op title menu (coop_lobby.h) covers the title and takes the keys; Esc ("Start
# Solo") gives the game's own title back (the native session is set by the inis, not by that menu yet).
if ($CoopPlus) { Start-Sleep 8; & "$here\actpid.ps1" -GamePid $h.Id -Seq "k:ESC:120 w:1500 shot:$Name-01-host-menu-closed" | Out-Null }
& "$here\keyspid.ps1" -GamePid $h.Id -Keys E | Out-Null
Start-Sleep $LoadWait
& $shot -Out "$here\shots\$Name-10-host-loaded.png" | Out-Null
if ($Ride) {
    # tc30: H alone sent no FrameEventCallMercenaryReq either; tc31+: the F1 wheel (hold F1, cursor on the horse slot at
    # the bottom, release), as the 3479 dragon tests summoned Blackstar.
    try {
        "host alone calls its horse at $(Get-Date -Format HH:mm:ss.fff)"
        & "$here\actpid.ps1" -GamePid $h.Id -Seq "kd:F1 w:1500 shot:$Name-11-wheel at:956:790 w:700 shot:$Name-12-wheel-horse ku:F1 w:12000 shot:$Name-13-host-alone-horse" | Out-Null
    } catch { "solo horse step failed: $_" }
}

$gp = Start-Copy 'guest' ''
$gp = $gp | Where-Object { $_ -is [System.Diagnostics.Process] }
& $shot -Out "$here\shots\$Name-20-guest-title.png" | Out-Null
if ($CoopPlus) { Start-Sleep 8; & "$here\actpid.ps1" -GamePid $gp.Id -Seq "k:ESC:120 w:1500 shot:$Name-21-guest-menu-closed" | Out-Null }
& "$here\keyspid.ps1" -GamePid $gp.Id -Keys E | Out-Null
Start-Sleep 10
# A second press if the first only refreshed the slot list (tc7): harmless once the load has started.
if (-not (Select-String -Path "$bin\cdmp.guest.log" -Pattern 'LoginCharacterToFieldServerReq' -Quiet)) {
    & "$here\keyspid.ps1" -GamePid $gp.Id -Keys E | Out-Null
}
Start-Sleep $JoinWait
foreach ($pair in @(@{ s = $HostScript; p = $h; n = 'host' }, @{ s = $GuestScript; p = $gp; n = 'guest' })) {
    if (-not $pair.s) { continue }
    "$($pair.n): its dev script (Ctrl+Shift+F6) at $(Get-Date -Format HH:mm:ss.fff)"
    & "$here\actpid.ps1" -GamePid $pair.p.Id -Seq "kd:CTRL kd:SHIFT kd:F6 w:400 ku:F6 ku:SHIFT ku:CTRL w:20000 shot:$Name-38-$($pair.n)-after-script" | Out-Null
}
if ($GuestKeys) {
    $GuestKeys = $GuestKeys -replace "_", " "  # (bash splits PSARGS at spaces: steps joined with _)
    "guest keys: $GuestKeys at $(Get-Date -Format HH:mm:ss.fff)"
    & "$here\actpid.ps1" -GamePid $gp.Id -Seq "$GuestKeys shot:$Name-38b-guest-after-keys" | Out-Null
}
if ($HostKeys) {
    $HostKeys = $HostKeys -replace "_", " "
    "host keys at $(Get-Date -Format HH:mm:ss.fff)"
    & "$here/actpid.ps1" -GamePid $h.Id -Seq "$HostKeys shot:$Name-38c-host-after-keys" | Out-Null
}
if ($GuestWalkTo) {
    $xz = $GuestWalkTo -split '_'
    "guest walks to ($($xz[0]), $($xz[1])) at $(Get-Date -Format HH:mm:ss.fff)"
    $env:CD_PID = "$($gp.Id)"; cmd /c "python ""$here/camwalk/walkto.py"" $($xz[0]) $($xz[1]) 3 2>&1" | Select-Object -Last 3
    $env:CD_PID = ''
}
if ($HostScript -or $GuestScript -or $GuestKeys -or $HostKeys -or $GuestWalkTo) {
    & "$here\actpid.ps1" -GamePid $h.Id -Seq "w:3000 shot:$Name-39-host-view" | Out-Null
    & "$here\actpid.ps1" -GamePid $gp.Id -Seq "w:3000 shot:$Name-39-guest-view" | Out-Null
}
& $shot -Out "$here\shots\$Name-30-guest-joined.png" | Out-Null
if ($Walk) {
    # Movement sync (tc23+): both spawn on one spot facing the same way (same save). The guest walks ahead (4 s of W)
    # and the host's window is shot (the guest should stand ahead of its camera); then the host walks 7 s (past the
    # guest) and the guest's window is shot. The packet logs give each side's positions (sent Req / received Ack).
    # (tc22: walk_to.py's steering wandered at 12 m; straight walks need no steering.)
    try {
        "guest walks at $(Get-Date -Format HH:mm:ss.fff)"
        & "$here\actpid.ps1" -GamePid $gp.Id -Seq "kd:W w:4000 ku:W w:1500 shot:$Name-33-guest-walked" | Out-Null
        & "$here\actpid.ps1" -GamePid $h.Id -Seq "w:2500 shot:$Name-34-host-sees-guest" | Out-Null
        "host walks at $(Get-Date -Format HH:mm:ss.fff)"
        & "$here\actpid.ps1" -GamePid $h.Id -Seq "kd:W w:7000 ku:W w:1500 shot:$Name-35-host-walked" | Out-Null
        & "$here\actpid.ps1" -GamePid $gp.Id -Seq "w:2500 shot:$Name-36-guest-sees-host" | Out-Null
    } catch { "walk step failed: $_" }
    "walks done $(Get-Date -Format HH:mm:ss.fff)"
    if ($CoopPlus) {
        # The Co-op+ layer's overlay: the party frame is on screen while linked; F8 opens its settings panel.
        try {
            & "$here\actpid.ps1" -GamePid $h.Id -Seq "w:500 shot:$Name-60-host-coopplus-hud" | Out-Null
            & "$here\actpid.ps1" -GamePid $gp.Id -Seq "w:500 shot:$Name-61-guest-coopplus-hud" | Out-Null
            & "$here\actpid.ps1" -GamePid $h.Id -Seq "k:F8:120 w:2000 shot:$Name-62-host-coopplus-panel k:F8:120 w:800" | Out-Null
            # tc36+: the layer's own traffic. Ping: Ctrl+F3 opens the quick list, Ctrl+F3 again sends its first entry
            # ("Go here", with a map pin). Chat: Shift+F2, "hi", Enter (tc36: chat cannot open while the quick list is
            # still open, 8 s, so the ping is sent first). Challenge: Shift+F5 opens the picker, Shift+F5 sends (Duel),
            # Shift+F5 on the guest accepts.
            "co-op+ acts at $(Get-Date -Format HH:mm:ss.fff)"
            & "$here\actpid.ps1" -GamePid $h.Id -Seq "kd:CTRL k:F3:120 ku:CTRL w:900 kd:CTRL k:F3:120 ku:CTRL w:1500" | Out-Null
            & "$here\actpid.ps1" -GamePid $gp.Id -Seq "w:500 shot:$Name-63-guest-got-ping" | Out-Null
            & "$here\actpid.ps1" -GamePid $h.Id -Seq "kd:SHIFT k:F2:120 ku:SHIFT w:700 k:H:80 k:I:80 w:300 k:ENTER:120 w:1500" | Out-Null
            & "$here\actpid.ps1" -GamePid $gp.Id -Seq "w:500 shot:$Name-64-guest-got-chat" | Out-Null
            & "$here\actpid.ps1" -GamePid $h.Id -Seq "kd:SHIFT k:F5:120 ku:SHIFT w:1500 shot:$Name-65-host-challenge-picker kd:SHIFT k:F5:120 ku:SHIFT w:1500" | Out-Null
            & "$here\actpid.ps1" -GamePid $gp.Id -Seq "w:500 shot:$Name-66-guest-challenged kd:SHIFT k:F5:120 ku:SHIFT w:3000 shot:$Name-67-guest-accepted" | Out-Null
            & "$here\actpid.ps1" -GamePid $h.Id -Seq "w:1000 shot:$Name-68-host-challenge-on" | Out-Null
        } catch { "co-op+ overlay step failed: $_" }
    }
    if ($Ride) {
        # Mounts (tc29+): the host calls its horse (H held 1.5 s: a tap did nothing on 3549 in tc28), mounts (E, the
        # "Ride" prompt), rides forward and dismounts (R) (keys from the 3479 horse tests, NOTES/LOG.md D30);
        # the guest's window is shot while the host rides. MountSync / HorseFollow lines in both logs.
        try {
            "host calls its horse at $(Get-Date -Format HH:mm:ss.fff)"
            & "$here\actpid.ps1" -GamePid $h.Id -Seq "kd:F1 w:1500 at:956:790 w:700 ku:F1 w:12000 shot:$Name-38-host-horse k:E:150 w:5000 shot:$Name-39-host-mounted" | Out-Null
            & "$here\actpid.ps1" -GamePid $gp.Id -Seq "w:2500 shot:$Name-40-guest-sees-mounted" | Out-Null
            "host rides at $(Get-Date -Format HH:mm:ss.fff)"
            & "$here\actpid.ps1" -GamePid $h.Id -Seq "kd:W w:5000 ku:W w:2000 shot:$Name-41-host-rode" | Out-Null
            & "$here\actpid.ps1" -GamePid $gp.Id -Seq "w:2500 shot:$Name-42-guest-sees-rider" | Out-Null
            "host dismounts at $(Get-Date -Format HH:mm:ss.fff)"
            & "$here\actpid.ps1" -GamePid $h.Id -Seq "k:R:150 w:4000 shot:$Name-43-host-dismounted" | Out-Null
            & "$here\actpid.ps1" -GamePid $gp.Id -Seq "w:2500 shot:$Name-44-guest-sees-dismounted" | Out-Null
        } catch { "ride step failed: $_" }
        "ride done $(Get-Date -Format HH:mm:ss.fff)"
    }
    # UI pause block: the host opens its menu for 12 s; its world (and the guest's) must keep running.
    "host ESC at $(Get-Date -Format HH:mm:ss.fff)"
    & "$here\keyspid.ps1" -GamePid $h.Id -Keys ESC | Out-Null
    Start-Sleep 6
    & $shot -Out "$here\shots\$Name-37-host-menu.png" | Out-Null
    Start-Sleep 6
    & "$here\keyspid.ps1" -GamePid $h.Id -Keys ESC | Out-Null
    "host ESC closed at $(Get-Date -Format HH:mm:ss.fff)"
}
if ($Fight) {
    # Combat (tc32+): the bandit save's Bleed Bandits are at Rodbel Fishing Dock (~ -10238, -3958; NOTES/LOG.md
    # 2026-11-01); both walk there (walk_to.py, 20 m short), the host attacks for 15 s, both windows are shot. The
    # packet logs tell whether the bandits' actions / HP changes reach the guest.
    # tc33+: the 3479 camera walker (camwalk\walkto.py: camera forward and position read from the copy's memory,
    # CD_PID picks the copy; tc32's packet-log walker stopped at a cliff 70 m short).
    "host walks to the dock at $(Get-Date -Format HH:mm:ss.fff)"
    $env:CD_PID = "$($h.Id)"; cmd /c "python ""$here\camwalk\walkto.py"" -10238 -3958 20 2>&1" | Select-Object -Last 3
    "guest walks to the dock at $(Get-Date -Format HH:mm:ss.fff)"
    $env:CD_PID = "$($gp.Id)"; cmd /c "python ""$here\camwalk\walkto.py"" -10238 -3958 24 2>&1" | Select-Object -Last 3
    $env:CD_PID = ''
    try {
        & "$here\actpid.ps1" -GamePid $h.Id -Seq "w:1000 shot:$Name-50-host-at-dock" | Out-Null
        & "$here\actpid.ps1" -GamePid $gp.Id -Seq "w:1000 shot:$Name-51-guest-at-dock" | Out-Null
        "host attacks at $(Get-Date -Format HH:mm:ss.fff)"
        $seq = (1..30 | ForEach-Object { 'click:L:80 w:400' }) -join ' '
        & "$here\actpid.ps1" -GamePid $h.Id -Seq "$seq shot:$Name-52-host-fought" | Out-Null
        & "$here\actpid.ps1" -GamePid $gp.Id -Seq "w:1500 shot:$Name-53-guest-sees-fight" | Out-Null
    } catch { "fight step failed: $_" }
    "fight done $(Get-Date -Format HH:mm:ss.fff)"
    if ($CoopPlus) {
        # Revive (tc39+): the bandits usually bring the host down (tc38: "Guest can revive you (30 s)"). tc39: the
        # guest pressed while the host still had 96 HP (it fell 30 s later, 8 m away): first wait for the host's
        # Co-op+ status line to say hp 0 (60 s at most), then the guest walks to where the host lies (its packet log)
        # and presses ReviveFriend (Shift+F7) there.
        if ($Kill -and -not (Select-String -Path "$bin\coopplus.host.log" -Pattern 'Local: .* hp 0\.0/' -Quiet)) {
            "host: the self-kill script (Ctrl+Shift+F6) at $(Get-Date -Format HH:mm:ss.fff)"
            & "$here\actpid.ps1" -GamePid $h.Id -Seq "kd:CTRL kd:SHIFT kd:F6 w:400 ku:F6 ku:SHIFT ku:CTRL w:4000 shot:$Name-53b-host-killed" | Out-Null
        }
        $t0 = Get-Date
        while (((Get-Date) - $t0).TotalSeconds -lt 60) {
            if (Select-String -Path "$bin\coopplus.host.log" -Pattern 'Local: .* hp 0\.0/' -Quiet) { break }
            Start-Sleep 1
        }
        "host down (or 60 s) at $(Get-Date -Format HH:mm:ss.fff)"
        $hlog = Get-ChildItem "$bin\cdmp_packets.host_*.log" | Sort-Object LastWriteTime -Descending | Select-Object -First 1
        $hp = (python "$here\pos.py" $hlog.FullName).Trim()
        "host at $hp; the guest goes to revive at $(Get-Date -Format HH:mm:ss.fff)"
        if ($hp) {
            $xz = $hp.Split(' ')
            $env:CD_PID = "$($gp.Id)"; cmd /c "python ""$here\camwalk\walkto.py"" $($xz[0]) $($xz[1]) 2.0 2>&1" | Select-Object -Last 3
            $env:CD_PID = ''
            try {
                & "$here\actpid.ps1" -GamePid $gp.Id -Seq "w:300 shot:$Name-54-guest-by-host kd:SHIFT kd:F7 w:3800 ku:F7 ku:SHIFT w:2500 shot:$Name-55-guest-revived" | Out-Null
                & "$here\actpid.ps1" -GamePid $h.Id -Seq "w:1500 shot:$Name-56-host-after-revive" | Out-Null
            } catch { "revive step failed: $_" }
        }
    }
}
& "$here\keyspid.ps1" -GamePid $h.Id -Keys SPACE | Out-Null
Start-Sleep 3
& $shot -Out "$here\shots\$Name-31-host.png" | Out-Null
Start-Sleep $Hold
& $shot -Out "$here\shots\$Name-40-later.png" | Out-Null
"host alive=$(-not $h.HasExited) guest alive=$(-not $gp.HasExited)"
if ($gp.HasExited) { "guest exit code 0x{0:X8} at {1}" -f $gp.ExitCode, $gp.ExitTime }
if ($h.HasExited) { "host exit code 0x{0:X8} at {1}" -f $h.ExitCode, $h.ExitTime }
