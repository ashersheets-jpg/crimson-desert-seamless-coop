# (by PID: two copies) A sequence of inputs to the game window (only while it is in front), for exploring a game mechanic by hand.
# Steps: kd:KEY / ku:KEY (key down / up; names as keys.ps1 or a letter), k:KEY:MS (press for MS), md:L|R|M / mu:L|R|M
# (mouse button down / up), click:L|R:MS, wheel:N:COUNT (N = +120 up / -120 down, COUNT times, 60 ms apart),
# move:DX:DY:STEPS, circle:R:TURNS:MS (move the mouse in circles of radius R px, clockwise, TURNS in MS each),
# w:MS (wait), shot:PATH (half-size screenshot).
param([int]$GamePid, [string]$Seq)
$Steps = $Seq -split ' ' | Where-Object { $_ }
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
Add-Type @'
using System; using System.Runtime.InteropServices;
public static class A {
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
  [DllImport("user32.dll")] public static extern uint MapVirtualKey(uint code, uint type);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, int dx, int dy, int d, UIntPtr e);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
  [DllImport("user32.dll")] public static extern bool BringWindowToTop(IntPtr h);
}
'@
$p = Get-Process -Id $GamePid -ErrorAction Stop | Where-Object { $_.MainWindowHandle -ne 0 }
$h = $p.MainWindowHandle
# Up to 24 tries (another window may take the focus for a moment): Alt tap, restore, bring to top, focus.
# 2026-11-09 (tcB244 / 246 / 247): while the game loads, the window in front can be ANOTHER window of the same
# game process than MainWindowHandle; in front = the foreground window belongs to this pid.
Add-Type -Namespace Fg -Name W -MemberDefinition '[DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint p);'
function FrontPid { $fp = 0; [void][Fg.W]::GetWindowThreadProcessId([A]::GetForegroundWindow(), [ref]$fp); [int]$fp }
for ($try = 0; $try -lt 24 -and (FrontPid) -ne $GamePid; $try++) {
  [A]::keybd_event(0x12, 0, 0, [UIntPtr]::Zero); [A]::keybd_event(0x12, 0, 2, [UIntPtr]::Zero)
  [A]::ShowWindow($h, 9) | Out-Null; [A]::BringWindowToTop($h) | Out-Null
  [A]::SetForegroundWindow($h) | Out-Null; Start-Sleep -Milliseconds 500
}
if ((FrontPid) -ne $GamePid) {
  $q = Get-Process -Id (FrontPid) -ErrorAction SilentlyContinue
  throw "game window is not in front (in front: pid $(FrontPid) $($q.Name) '$($q.MainWindowTitle)')"
}
$vk = @{ ENTER=0x0D; ESC=0x1B; SPACE=0x20; UP=0x26; DOWN=0x28; LEFT=0x25; RIGHT=0x27; CTRL=0x11; TAB=0x09; ALT=0x12;
         SHIFT=0x10; F1=0x70; F2=0x71; F3=0x72; F4=0x73; F5=0x74; F6=0x75; F7=0x76; F8=0x77; F9=0x78; F10=0x79 }
function code($n) { if ($vk.ContainsKey($n)) { [byte]$vk[$n] } else { [byte][char]$n } }
function key($c, $up) { [A]::keybd_event($c, [byte][A]::MapVirtualKey($c, 0), $(if ($up) { 2 } else { 0 }), [UIntPtr]::Zero) }
$btn = @{ L = @(2, 4); R = @(8, 16); M = @(32, 64) }
function shot($out) {
  $b = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
  $bmp = New-Object System.Drawing.Bitmap $b.Width, $b.Height
  [System.Drawing.Graphics]::FromImage($bmp).CopyFromScreen($b.Location, [System.Drawing.Point]::Empty, $b.Size)
  (New-Object System.Drawing.Bitmap $bmp, ([int]($b.Width / 2)), ([int]($b.Height / 2))).Save($out, [System.Drawing.Imaging.ImageFormat]::Png)
}
foreach ($s in $Steps) {
  if ((FrontPid) -ne $GamePid) { throw "game lost focus before $s" }
  $a = $s.Split(':')
  switch ($a[0]) {
    'kd' { key (code $a[1]) $false }
    'ku' { key (code $a[1]) $true }
    'k' { $c = code $a[1]; key $c $false; Start-Sleep -Milliseconds ([int]$a[2]); key $c $true }
    'md' { [A]::mouse_event($btn[$a[1]][0], 0, 0, 0, [UIntPtr]::Zero) }
    'mu' { [A]::mouse_event($btn[$a[1]][1], 0, 0, 0, [UIntPtr]::Zero) }
    'click' { [A]::mouse_event($btn[$a[1]][0], 0, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds ([int]$a[2]); [A]::mouse_event($btn[$a[1]][1], 0, 0, 0, [UIntPtr]::Zero) }
    'wheel' { for ($i = 0; $i -lt [int]$a[2]; $i++) { [A]::mouse_event(0x800, 0, 0, [int]$a[1], [UIntPtr]::Zero); Start-Sleep -Milliseconds 60 } }
    'move' { $n = [int]$a[3]; for ($i = 0; $i -lt $n; $i++) { [A]::mouse_event(1, [int]([int]$a[1] / $n), [int]([int]$a[2] / $n), 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 15 } }
    'circle' {
      $r = [double]$a[1]; $turns = [int]$a[2]; $ms = [int]$a[3]; $seg = 24
      for ($t = 0; $t -lt $turns; $t++) { for ($i = 0; $i -lt $seg; $i++) {
        $a0 = 2 * [Math]::PI * $i / $seg; $a1 = 2 * [Math]::PI * ($i + 1) / $seg
        [A]::mouse_event(1, [int]($r * ([Math]::Cos($a1) - [Math]::Cos($a0))), [int]($r * ([Math]::Sin($a1) - [Math]::Sin($a0))), 0, [UIntPtr]::Zero)
        Start-Sleep -Milliseconds ([int]($ms / $seg)) } }
    }
    'at' { [A]::SetCursorPos([int]$a[1], [int]$a[2]) | Out-Null; Start-Sleep -Milliseconds 80 }
    'w' { Start-Sleep -Milliseconds ([int]$a[1]) }
    'shot' { shot (Join-Path (Join-Path $PSScriptRoot 'shots') ($a[1] + '.png')) }  # shot:NAME -> shots/NAME.png (no drive letters: steps split on ':')
  }
}
"done"
