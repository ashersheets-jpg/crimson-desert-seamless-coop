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
      # B22a test (-Lobby): 'host' | 'guest': that role's co-op save redirect counts as failed (a flag file the
      # native base checks in co-op mode only); co-op must be refused and the game must stay a solo game.
      [string]$RefuseCoop = '',
      # 2026-11-08: the host alone (no guest copy): its script, walk and keys, then -Hold (a solo baseline in the host's mode)
      [switch]$NoGuest,
      # B17 (2026-11-09): the guest revives the host by holding E (the interact key) instead of Shift+F7
      [switch]$ReviveWithE,
      # A Co-op+ dev script per role ([dev] script=..., run with Ctrl+Shift+F6 after the join), e.g. a teleport
      [string]$HostScript = '', [string]$GuestScript = '',
      # actpid steps for the guest after the scripts (e.g. 'kd:CTRL k:F4:120 ku:CTRL w:20000': go to the friend)
      [string]$GuestKeys = '',
      # actpid steps for the host after the guest's (joined with _ like -GuestKeys)
      [string]$HostKeys = '',
      # 'x_z': the guest walks there after the scripts and keys (camwalk\walkto.py, 3 m short)
      [string]$GuestWalkTo = '',
      # 'x_z': a first point the guest walks to before -GuestWalkTo (0.6 m short), e.g. to line up with a doorway
      [string]$GuestWalkVia = '',
      # the same for the host (after the guest's walk); then -AfterWalkKeys (actpid steps, _-joined) on the host
      [string]$HostWalkTo = '', [string]$AfterWalkKeys = '', [string]$GuestAfterWalkKeys = '',
      # host ini overrides 'key=value,key=value' (an existing line of that key is replaced)
      [string]$HostSet = '',
      # k=v,k=v for the guest's [session2] (replaces or appends, like -HostSet)
      [string]$GuestSet = '',
      # -Lobby research runs: k=v,k=v added to the host's native ini by its layer ([dev] native_extra)
      [string]$HostNativeExtra = '',
      # 2026-11-12 (gn3): a screenshot every 4 s while a copy is waited into the shared world (NAME-W<role><n>), at most 40
      [switch]$WaitShots,
      # 2026-11-12 (gn4): the guest has no save: its game says so and goes back to solo. The flow stops there (two
      # screenshots, NAME-nosave-guest / -host) instead of waiting 19 minutes for a join that cannot come.
      [switch]$ExpectNoSave,
      # 2026-11-09 (stability pass): -GuestLeave = after -Hold the guest's game is killed (a crash / Alt+F4), the
      # host plays on for 60 s (its final save of the guest's slot, the mailbox). -KeepSlots = the host's co-op
      # folder keeps its other slots (the game's autosaves of earlier runs: 'Continue' loads the newest).
      [switch]$GuestLeave, [switch]$KeepSlots, [switch]$SecondRound, [switch]$ExpectMismatch,
      # 2026-11-10 (Steam overlay / invite test): the host alone at the title lobby for -TitleHold seconds (the test
      # drives it with actpid from outside), its layer on -HostTransport (steam = a real Steam lobby, role auto)
      [int]$TitleHold = 0, [string]$HostTransport = 'udp',
      # 2026-11-10: k=v,k=v added to the host layer's [dev] section (e.g. pad_sim=1)
      [string]$HostDev = '', [string]$GuestDev = '',
      # 2026-11-11 (testers: no Co-op menu at the title): -Fresh = the layer ini of a first-time player (no onboarded /
      # experimental_asked marks); -KeepDisplay = the player's own display mode instead of Windowed
      [switch]$Fresh, [switch]$KeepDisplay,
      # -FrameGen = frame generation ON in the test copies' settings (and no forced lowest quality / 50% scale)
      [switch]$FrameGen,
      # with -FrameGen: the frame generation method and the upscaler by the game's own names, e.g. 'Intel XeSS Frame
      # Generation' + 'Intel XeSS 3.0', 'AMD FSR Frame Generation'
      [string]$FrameGenMode = '', [string]$Upscale = '',
      # -PackageIni <a package's mod folder>: the layer ini of both copies = that package's coopplus.ini (a first-time
      # player's real defaults) with only the transport / role / port lines changed for two copies on one PC
      [string]$PackageIni = '',
      # 2026-11-11: which of the host's "How does your co-op journey begin?" choices the lobby flow takes: 1 = the first
      # (Continue Our Co-op Save when one exists; what every run before today took, whatever its log line said), 2 = Start
      # From My Solo Progress, 3 = New Co-op Save (then the difficulty question gets one more Space). 3 REPLACES the
      # test co-op world: back up %LOCALAPPDATA%\Pearl Abyss\CD\coop_worlds\with_127_0_0_1 first and put it back.
      [int]$HostChoice = 1)
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
record=0
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
if ($HostSet) {
    $hi = "$bin\cdmp.host.ini"; $t = [IO.File]::ReadAllText($hi, [Text.Encoding]::Unicode)
    foreach ($kv in $HostSet -split ',') {
        $k, $v = $kv -split '=', 2
        if ($t -match "(?m)^$([regex]::Escape($k))=") { $t = [regex]::Replace($t, "(?m)^$([regex]::Escape($k))=.*$", "$k=$v") }
        else { $t = $t.TrimEnd() + "`r`n$k=$v`r`n" }  # [session2] is the last section
    }
    [IO.File]::WriteAllText($hi, $t, (New-Object Text.UnicodeEncoding($false, $true))); "host ini overrides: $HostSet"
}
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
record=0
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
if ($GuestSet) {
    $gi = "$bin\cdmp.guest.ini"; $t = [IO.File]::ReadAllText($gi, [Text.Encoding]::Unicode)
    foreach ($kv in $GuestSet -split ',') {
        $k, $v = $kv -split '=', 2
        if ($t -match "(?m)^$([regex]::Escape($k))=") { $t = [regex]::Replace($t, "(?m)^$([regex]::Escape($k))=.*$", "$k=$v") }
        else { $t = $t.TrimEnd() + "`r`n$k=$v`r`n" }  # [session2] is the last section
    }
    [IO.File]::WriteAllText($gi, $t, (New-Object Text.UnicodeEncoding($false, $true))); "guest ini overrides: $GuestSet"
}

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
        # 2026-11-09: -HostNativeExtra 'k=v,k=v' -> the host layer's [dev] native_extra (added to the native ini the
        # lobby writes: research runs with keys the release defaults keep off)
        if ($HostDev -and $r -eq 'host') { $hd = ($HostDev -split ',') -join "`n"; $dev = $(if ($dev) { "$dev`n$hd" } else { "[dev]`n$hd" }) }
        if ($GuestDev -and $r -eq 'guest') { $hd = ($GuestDev -split ',') -join "`n"; $dev = $(if ($dev) { "$dev`n$hd" } else { "[dev]`n$hd" }) }
        if ($HostNativeExtra -and $r -eq 'host') { $dev = $(if ($dev) { "$dev`nnative_extra=$HostNativeExtra" } else { "[dev]`nnative_extra=$HostNativeExtra" }) }
        # 2026-11-14: the owner's PC switched itself off twice during two-copy runs (Kernel-Power 41, bugcheck 0: a
        # hardware cut-off under load). Every test copy is held to 40 frames a second by the layer ([dev] fps_cap).
        # 2026-11-18: a cap given with -HostDev / -GuestDev (fps_cap=N, the running-freeze test with different FPS on the two copies) replaces the default 40.
        if ($dev -notmatch '(?m)^fps_cap=') { $dev = $(if ($dev) { "$dev`nfps_cap=40" } else { "[dev]`nfps_cap=40" }) }
        if ($PackageIni) {
            $pk = [IO.File]::ReadAllText((Join-Path $PackageIni 'coopplus.ini'), [Text.Encoding]::Unicode)
            $tr = $(if ($r -eq 'host') { $HostTransport } else { 'udp' })
            $ro = $(if ($r -eq 'host' -and $HostTransport -eq 'steam') { 'auto' } else { $r })
            $pk = $pk -replace '(?m)^player_name=.*$', "player_name=$who" -replace '(?m)^transport=.*$', "transport=$tr" -replace '(?m)^role=.*$', "role=$ro" -replace '(?m)^peer_port=.*$', 'peer_port=37115'
            Write-Ini "$bin\coopplus.$r.ini" ($pk.TrimEnd() + "`n" + $dev + "`n")
            "layer ini ($r): the package's coopplus.ini (transport $tr, role $ro)"
        } else {
        Write-Ini "$bin\coopplus.$r.ini" @"
[general]
player_name=$who
$(if (-not $Fresh) { "onboarded=1`nexperimental_asked=1" })
[net]
enabled=1
transport=$(if ($r -eq 'host') { $HostTransport } else { 'udp' })
role=$(if ($r -eq 'host' -and $HostTransport -eq 'steam') { 'auto' } else { $r })
peer_host=127.0.0.1
peer_port=37115
[coop]
enabled=1
[overlay]
enabled=1
$dev
"@
        }
    }
    # No relay: Co-op+'s host binds peer_port itself and its guest connects to it (tc34: a relay on the same port kept
    # the host from binding until the relay quit).
    "co-op+ layer: coopplus.asi, host on udp 37115"
}

# The windowed settings (template: the player's own file, read only).
$xml = [IO.File]::ReadAllText("$cd\save\user_engine_option_save.xml")
$win = $(if ($KeepDisplay) { $xml } else { $xml -replace '(<EnumSelectDisplayType Name="_displayType" _select=")[^"]*"', '$1Windowed"' })
if (-not $KeepDisplay -and $win -eq $xml -and $xml -notmatch '_select="Windowed"') { throw 'display type not found in the settings file' }
# Two copies on one GPU: the lowest quality in both test copies (the player's own settings file is never written).
$win = $win -replace '(<EnumSelectQualityLevel Name="\w+" _select=")[^"]*"', '$1Minimum"'
$win = $win -replace '(<OptionStringVector Name="_postProcessingQualityLevelSelect" _value=")[^"]*"', '$1Minimum"'
$win = $win -replace '(<EnumSelectResolutionScale Name="_upscaleResolution" _select=")[^"]*"', '$1 50%"' -replace '_select=" 50%"', '_select="50%"'
if ($FrameGen) {
    $win = $(if ($KeepDisplay) { $xml } else { $xml -replace '(<EnumSelectDisplayType Name="_displayType" _select=")[^"]*"', '$1Windowed"' })
    $win = $win -replace '<OptionBool Name="_enableFrameGeneration"( _value="[^"]*")?', '<OptionBool Name="_enableFrameGeneration" _value="True"'
    if ($win -notmatch '_enableFrameGeneration" _value="True"') { throw 'frame generation option not found in the settings file' }
    $FrameGenMode = $FrameGenMode -replace '_', ' '; $Upscale = $Upscale -replace '_', ' '  # PSARGS has no spaces
    if ($FrameGenMode) { $win = $win -replace '(<OptionStringVector Name="_frameGenerationModeSelect" _value=")[^"]*"', ('$1' + $FrameGenMode + '"') }
    if ($Upscale) { $win = $win -replace '(<OptionStringVector Name="_upscaleModeSelect" _value=")[^"]*"', ('$1' + $Upscale + '"') }
}
[IO.File]::WriteAllText("$here\settings-windowed.xml", $win)

# The guest's own save folder (only ever made by this script; marked).
$g = "$cd\save_cdmp_guest"
if ((Test-Path $g) -and -not (Test-Path "$g\.coopplus-test")) { throw "$g exists and is not ours: left alone, test not started" }
if (Test-Path $g) { Remove-Item $g -Recurse -Force }
New-Item -ItemType Directory "$g\$acct" -Force | Out-Null
Set-Content "$g\.coopplus-test" 'made by build/native/twocopy.ps1'
# -GuestVariant none (2026-11-11): a guest who has never played: no slot at all in its save folder
if ($GuestVariant -ne 'none') { Copy-Item "$saves\slot103-$GuestVariant" "$g\$acct\slot103" -Recurse }
Copy-Item "$here\settings-windowed.xml" "$g\user_engine_option_save.xml"
Copy-Item "$cd\save\initialize_input.save" "$g\initialize_input.save"

Remove-Item "$bin\cdmp.host.log", "$bin\cdmp.guest.log" -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force "$here\shots" | Out-Null
$env:SteamAppId = '3321460'; $env:SteamGameId = '3321460'
function Start-Copy([string]$role, [string]$add) {
    $env:CDMP_ROLE = $role; $env:CDMP_SANDBOX_ADD = $add
    $env:CDMP_SANDBOX_ONLY = $(if ($role -eq 'host' -and -not $KeepSlots) { "$acct\slot103" } else { '' })
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
Remove-Item "$bin\cdmp_test_refuse_coop.host.flag", "$bin\cdmp_test_refuse_coop.guest.flag" -ErrorAction SilentlyContinue
if ($RefuseCoop) { Set-Content -Path "$bin\cdmp_test_refuse_coop.$RefuseCoop.flag" -Value 'test'; "B22a test: the $RefuseCoop's co-op save redirect counts as failed" }
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
# -HostChoice 3 (New Co-op Save): the host gets NO test slot, or the "new" co-op world would be filled with it and
# the game would continue that save instead of starting the story (nc1, 2026-11-11).
# -HostVariant keep (2026-11-12, fs2): the co-op world as the last run left it (Continue of a world a test made).
$hostAdd = $(if ($HostChoice -eq 3 -or $HostVariant -eq 'keep') { "$here\settings-windowed.xml|user_engine_option_save.xml" } else { "$saves\slot103-$HostVariant|$acct\slot103;$here\settings-windowed.xml|user_engine_option_save.xml" })
$h = Start-Copy 'host' $hostAdd
$h = $h | Where-Object { $_ -is [System.Diagnostics.Process] }
& $shot -Out "$here\shots\$Name-00-host-title.png" | Out-Null
if ($TitleHold) {
    "title hold: host pid $($h.Id) for $TitleHold s"
    if ($HostKeys) {  # 2026-11-11: keys at the title (the lobby menu's own keys)
        "title keys at $(Get-Date -Format HH:mm:ss.fff)"
        & "$here/actpid.ps1" -GamePid $h.Id -Seq ($HostKeys -replace "_", " ") | Out-Null
    }
    $third = [int]($TitleHold / 3)
    foreach ($k in 1, 2, 3) { Start-Sleep $third; & $shot -Out "$here\shots\$Name-0$k-title-hold.png" | Out-Null }
    exit 0
}
if ($Lobby) {
    $gp = Start-Copy 'guest' ''
    $gp = $gp | Where-Object { $_ -is [System.Diagnostics.Process] }
    Start-Sleep 15
    & $shot -Out "$here\shots\$Name-L0-both-titles.png" | Out-Null
    if ($ExpectMismatch) {
        # mm1 (2026-11-12): the two copies say they are different builds ([dev] build_id on one): the handshake refuses,
        # both title menus must say so, nothing starts. A shot of each window, then the usual end.
        Start-Sleep 15
        try { & "$here\actpid.ps1" -GamePid $h.Id -Seq "w:1500 shot:$Name-mismatch-host" | Out-Null } catch { "host shot: $($_.Exception.Message)" }
        try { & "$here\actpid.ps1" -GamePid $gp.Id -Seq "w:1500 shot:$Name-mismatch-guest" | Out-Null } catch { "guest shot: $($_.Exception.Message)" }
        "lobby flow done (two builds: no start expected)"
        exit 0
    }
    "host: Start Co-op, then choice $HostChoice of the question (1 = the first: Continue Our Co-op Save when one exists) at $(Get-Date -Format HH:mm:ss.fff)"
    & "$here\actpid.ps1" -GamePid $h.Id -Seq "at:960:462 w:250 at:966:466 w:400 k:S:80 w:500 k:S:80 w:500 k:S:80 w:700 shot:$Name-L1-host-start-coop k:SPACE:120 w:1500 shot:$Name-L2-host-question $(('k:S:80 w:500 ' * [Math]::Max(0, $HostChoice - 1)))shot:$Name-L2b-host-choice k:SPACE:120 w:1000" | Out-Null
    if ($HostChoice -eq 3) {
        # nc1 (2026-11-11): after the choice the game may ask one more thing (the difficulty) or restart at once; a
        # restart takes the window away, which must not stop the test.
        try { & "$here\actpid.ps1" -GamePid $h.Id -Seq "w:1500 shot:$Name-L2c-after-choice k:SPACE:120 w:1500 shot:$Name-L2d-after-space" | Out-Null } catch { "after the New Co-op Save choice: $($_.Exception.Message)" }
    }
    # The host restarts, then the guest: wait for both new processes and the guest's connection to the host's world.
    $t0 = Get-Date; $remote = $false; $nosave = $false
    while (((Get-Date) - $t0).TotalSeconds -lt 420) {
        Start-Sleep 5
        if ($ExpectNoSave -and (Get-ChildItem "$bin\coopplus.guest*.log" -ErrorAction SilentlyContinue | Where-Object { $_.LastWriteTime -gt $t0 } | Select-String -Pattern 'no save of their own' -Quiet)) { $nosave = $true; break }
        if ((Test-Path "$bin\cdmp.guest.log") -and (Select-String -Path "$bin\cdmp.guest.log" -Pattern '-> REMOTE' -Quiet)) { $remote = $true; break }
    }
    if ($nosave) {
        "guest has no save: its game goes back to solo; seen at $(Get-Date -Format HH:mm:ss.fff)"
        Start-Sleep 50
        $two = @(Get-Process CrimsonDesert -ErrorAction SilentlyContinue | Sort-Object StartTime | Select-Object -Last 2)
        Get-Process CrimsonDesert -ErrorAction SilentlyContinue | ForEach-Object { "running: pid $($_.Id) started $($_.StartTime.ToString('HH:mm:ss'))" }
        if ($two.Count -eq 2) {
            try { & "$here\actpid.ps1" -GamePid $two[1].Id -Seq "w:1500 shot:$Name-nosave-guest" | Out-Null } catch { "guest shot: $($_.Exception.Message)" }
            try { & "$here\actpid.ps1" -GamePid $two[0].Id -Seq "w:1500 shot:$Name-nosave-host" | Out-Null } catch { "host shot: $($_.Exception.Message)" }
        }
        foreach ($r in 'host', 'guest') { Copy-Item "$bin\coopplus.$r.1.log" "$here\$Name.coopplus.$r.before.log" -ErrorAction SilentlyContinue }
        "lobby flow done (the guest has no save)"
        exit 0
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
        if (Select-String -Path "$bin\coopplus.host.log" -Pattern 'both games start in|loads the shared world first' -Quiet) { break }
    }
    $new = @(Get-Process CrimsonDesert -ErrorAction SilentlyContinue | Sort-Object StartTime | Select-Object -Last 2)
    $follow = {
        param($new, $tag)
        "countdown seen$tag; host pid $($new[0].Id), guest pid $($new[1].Id): each window in front for its Play"
        # 2026-11-09: on the native base the host loads first, the guest once the host is in: each window in front
        # until its game is in the shared world (a player's window is in front anyway).
        foreach ($k in @(@{ p = $new[0]; r = 'host' }, @{ p = $new[1]; r = 'guest' })) {
            $t1 = Get-Date; $step = 0
            while (((Get-Date) - $t1).TotalSeconds -lt 240) {
                # -HostChoice 3 (nc2, 2026-11-11): a new co-op world is a fresh save folder, so the HOST's game shows the
                # End User License Agreement again, then its own New Game steps. In this TEST copy the harness answers
                # with Space every 4 s (a shot before each press shows what was on screen).
                if ($HostChoice -eq 3 -and $k.r -eq 'host') {
                    $step++
                    # nc4: the agreement's Accept only works once the text is scrolled to its END (the mod does the same
                    # for the guest, coop_worlds.cpp worlds_lobby_guest_new): mouse over the text, wheel down, hold Space.
                    # First three steps: that; afterwards a plain Space (the difficulty's Confirm and whatever follows).
                    $seq = $(if ($step -le 3) { "shot:$Name-N$('{0:d2}' -f $step) at:960:540 w:300 wheel:-120:200 w:800 k:SPACE:1700 w:2500" } else { "shot:$Name-N$('{0:d2}' -f $step) k:SPACE:400 w:3600" })
                    try { & "$here\actpid.ps1" -GamePid $k.p.Id -Seq $seq | Out-Null } catch { "new game step ${step}: $($_.Exception.Message)" }
                    if (Select-String -Path "$bin\coopplus.$($k.r).log" -Pattern 'in the shared world' -Quiet) { break }
                    continue
                }
                $step++
                & "$here\actpid.ps1" -GamePid $k.p.Id -Seq $(if ($WaitShots -and $step -le 40) { "w:3600 shot:$Name-W$tag$($k.r)$('{0:d2}' -f $step)" } else { "w:4000" }) | Out-Null
                if (Select-String -Path "$bin\coopplus.$($k.r).log" -Pattern 'in the shared world' -Quiet) { break }
            }
            "$($k.r) in the shared world$tag`: $(Select-String -Path "$bin\coopplus.$($k.r).log" -Pattern 'in the shared world' -Quiet) at $(Get-Date -Format HH:mm:ss.fff)"
            & $shot -Out "$here\shots\$Name-L3$tag-$($k.r)-in.png" | Out-Null
        }
    }
    if ($new.Count -eq 2) { & $follow $new '' }
    if ($SecondRound) {
        # rt3 (2026-11-12): the recovery after a start that did not work. The host's layer ([dev] lobby_recover=1)
        # leaves the session once the guest said it is stuck and chooses Start Co-op again at the title; both games
        # restart a second time. Here: wait for the host's marker, then for the NEW host process's "loads the
        # shared world first" (its log starts anew at each launch), then the same windows-in-front as above.
        # First the moment in between: the host has left, both are at the title and still in one party.
        $t0 = Get-Date
        while (((Get-Date) - $t0).TotalSeconds -lt 120) {
            Start-Sleep 1
            if (Select-String -Path "$bin\coopplus.host.log" -Pattern 'the session ended by a Leave|own Return to Title now' -Quiet) { break }
        }
        Start-Sleep 5
        try { & "$here\actpid.ps1" -GamePid $new[1].Id -Seq "w:800 shot:$Name-R0-guest-after-leave" | Out-Null } catch { "guest shot after the leave: $($_.Exception.Message)" }
        try { & "$here\actpid.ps1" -GamePid $new[0].Id -Seq "w:800 shot:$Name-R0-host-after-leave" | Out-Null } catch { "host shot after the leave: $($_.Exception.Message)" }
        $t0 = Get-Date; $marker = $false
        while (((Get-Date) - $t0).TotalSeconds -lt 180) {
            Start-Sleep 2
            if (Select-String -Path "$bin\coopplus.host.log" -Pattern 'lobby_recover: back at the title' -Quiet) { $marker = $true; break }
        }
        "second round: the host's new Start Co-op seen: $marker at $(Get-Date -Format HH:mm:ss.fff)"
        & $shot -Out "$here\shots\$Name-R0-host-chose-again.png" | Out-Null
        $t0 = Get-Date; $go = $false
        while (((Get-Date) - $t0).TotalSeconds -lt 300) {
            Start-Sleep 2
            if ((Select-String -Path "$bin\coopplus.host.log" -Pattern 'both games start in|loads the shared world first' -Quiet) -and
                -not (Select-String -Path "$bin\coopplus.host.log" -Pattern 'lobby_recover: back at the title' -Quiet)) { $go = $true; break }
        }
        "second round: both games are in co-op again: $go at $(Get-Date -Format HH:mm:ss.fff)"
        Get-Process CrimsonDesert -ErrorAction SilentlyContinue | ForEach-Object { "running: pid $($_.Id) started $($_.StartTime.ToString('HH:mm:ss'))" }
        $new = @(Get-Process CrimsonDesert -ErrorAction SilentlyContinue | Sort-Object StartTime | Select-Object -Last 2)
        if ($new.Count -eq 2) { & $follow $new '2' }
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
    # 2026-11-09 (B02b release check): the release flow, then the usual scripts / walks on the restarted copies.
    if (-not ($HostScript -or $GuestScript -or $GuestWalkTo -or $HostWalkTo -or $GuestAfterWalkKeys -or $AfterWalkKeys -or $HostKeys -or $GuestKeys) -or $new.Count -ne 2) { exit 0 }
    $h = $new[0]; $gp = $new[1]
    $JoinWait = 0
    $LobbyGo = $true
}
# tcB2: with -CoopPlus the layer's co-op title menu (coop_lobby.h) covers the title and takes the keys; Esc ("Start
# Solo") gives the game's own title back (the native session is set by the inis, not by that menu yet).
if (-not $LobbyGo) {
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

if ($NoGuest) {
    if ($HostScript) {
        "host: its dev script (Ctrl+Shift+F6) at $(Get-Date -Format HH:mm:ss.fff)"
        & "$here\actpid.ps1" -GamePid $h.Id -Seq "kd:CTRL kd:SHIFT kd:F6 w:400 ku:F6 ku:SHIFT ku:CTRL w:20000 shot:$Name-38-host-after-script" | Out-Null
    }
    if ($HostWalkTo) {
        $xz = $HostWalkTo -split '_'
        "host walks to ($($xz[0]), $($xz[1])) at $(Get-Date -Format HH:mm:ss.fff)"
        $env:CD_PID = "$($h.Id)"; cmd /c "python ""$here/camwalk/walkto.py"" $($xz[0]) $($xz[1]) 1.5 --walk $(if ($xz.Count -ge 4) { '--face=' + $xz[2] + ',' + $xz[3] }) $(if ($xz.Count -ge 9) { '--sweep=' + ($xz[4..($xz.Count - 1)] -join ',') }) 2>&1" | Select-Object -Last 3
        $env:CD_PID = ''
    }
    if ($AfterWalkKeys) {
        $AfterWalkKeys = $AfterWalkKeys -replace "_", " "
        "host after-walk keys at $(Get-Date -Format HH:mm:ss.fff)"
        & "$here\actpid.ps1" -GamePid $h.Id -Seq "$AfterWalkKeys shot:$Name-38d-host-after-walk-keys" | Out-Null
    }
    Start-Sleep $Hold
    "host alone: done"
    exit 0
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
}  # -not $LobbyGo
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
if ($GuestWalkVia) {
    $v = $GuestWalkVia -split '_'
    "guest walks via ($($v[0]), $($v[1])) at $(Get-Date -Format HH:mm:ss.fff)"
    $env:CD_PID = "$($gp.Id)"; cmd /c "python ""$here/camwalk/walkto.py"" $($v[0]) $($v[1]) 0.6 --walk 2>&1" | Select-Object -Last 2
    $env:CD_PID = ''
}
if ($GuestWalkTo) {
    $xz = $GuestWalkTo -split '_'
    "guest walks to ($($xz[0]), $($xz[1])) at $(Get-Date -Format HH:mm:ss.fff)"
    $env:CD_PID = "$($gp.Id)"; cmd /c "python ""$here/camwalk/walkto.py"" $($xz[0]) $($xz[1]) 1.5 --walk $(if ($xz.Count -ge 4) { '--face=' + $xz[2] + ',' + $xz[3] }) $(if ($xz.Count -ge 9) { '--sweep=' + ($xz[4..($xz.Count - 1)] -join ',') }) 2>&1" | Select-Object -Last 3
    $env:CD_PID = ''
}
if ($GuestAfterWalkKeys) {
    $GuestAfterWalkKeys = $GuestAfterWalkKeys -replace "_", " "
    "guest after-walk keys at $(Get-Date -Format HH:mm:ss.fff)"
    & "$here\actpid.ps1" -GamePid $gp.Id -Seq "$GuestAfterWalkKeys shot:$Name-38e-guest-after-walk-keys" | Out-Null
}
if ($HostWalkTo) {
    $xz = $HostWalkTo -split '_'
    "host walks to ($($xz[0]), $($xz[1])) at $(Get-Date -Format HH:mm:ss.fff)"
    $env:CD_PID = "$($h.Id)"; cmd /c "python ""$here/camwalk/walkto.py"" $($xz[0]) $($xz[1]) 1.5 --walk $(if ($xz.Count -ge 4) { '--face=' + $xz[2] + ',' + $xz[3] }) $(if ($xz.Count -ge 9) { '--sweep=' + ($xz[4..($xz.Count - 1)] -join ',') }) 2>&1" | Select-Object -Last 3
    $env:CD_PID = ''
}
if ($AfterWalkKeys) {
    $AfterWalkKeys = $AfterWalkKeys -replace "_", " "
    "host after-walk keys at $(Get-Date -Format HH:mm:ss.fff)"
    & "$here\actpid.ps1" -GamePid $h.Id -Seq "$AfterWalkKeys shot:$Name-38d-host-after-walk-keys" | Out-Null
}
if ($HostScript -or $GuestScript -or $GuestKeys -or $HostKeys -or $GuestWalkTo -or $HostWalkTo) {
    & "$here\actpid.ps1" -GamePid $h.Id -Seq "w:3000 shot:$Name-39-host-view" | Out-Null
    try { & "$here\actpid.ps1" -GamePid $gp.Id -Seq "w:3000 shot:$Name-39-guest-view" | Out-Null } catch { "guest view skipped: $_" }
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
                $rk = if ($ReviveWithE) { "kd:E w:3800 ku:E" } else { "kd:SHIFT kd:F7 w:3800 ku:F7 ku:SHIFT" }
                & "$here\actpid.ps1" -GamePid $gp.Id -Seq "w:300 shot:$Name-54-guest-by-host $rk w:2500 shot:$Name-55-guest-revived" | Out-Null
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
if ($GuestLeave) {
    "guest leaves (its game is killed) at $(Get-Date -Format HH:mm:ss.fff)"
    Stop-Process -Id $gp.Id -Force -ErrorAction SilentlyContinue
    Start-Sleep 60
    & "$here\actpid.ps1" -GamePid $h.Id -Seq "w:500 shot:$Name-70-host-after-guest-left k:W:1500 w:1000 shot:$Name-71-host-walks-on" | Out-Null
    "host running 60 s after the guest left: $(-not (Get-Process -Id $h.Id).HasExited)"
}
"host alive=$(-not $h.HasExited) guest alive=$(-not $gp.HasExited)"
if ($gp.HasExited) { "guest exit code 0x{0:X8} at {1}" -f $gp.ExitCode, $gp.ExitTime }
if ($h.HasExited) { "host exit code 0x{0:X8} at {1}" -f $h.ExitCode, $h.ExitTime }
