# Walk our character to a world position (x, z): steer the camera toward the target (camera forward read
# from memory, read-only), hold W (+Shift) in short bursts, stop on arrival, a fall or being stuck.
#   python walkto.py X Z [stop_m] [--walk]
#   (native two-copy harness: set CD_PID to the copy to drive; live.py reads that process)
import ctypes, math, os, struct, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from live import BASE, rd, q

# Per game build, from the running game's PE timestamp (2026-11-06: 1.0.0.3576 moved both; 3527 and 3549 share theirs).
_BUILDS = {0x6AED2E85: (0x71CF140, 0x70DC130),  # 3576 (build/port3576/map_3576.json)
           0x6AF5D198: (0x7253E60, 0x71602A8),  # 3663 (tools/native-port/p63.py: 2 of 2 code references each)
           0x6AFDC0CF: (0x725A040, 0x7166368),  # 3732 (build/port3732 fmap.data + portmap.data: 2 of 2 each; pp37 chest
                                                 # walked blind on 3663's values: "at (0, 0, 0)", stuck 41 s)
           0x6B07E5E9: (0x73FFE08, 0x730A338)}  # 3840 (= kStaticPlayerPos / kCameraSlot: 2 of 2 references each)
_pe = struct.unpack('<I', rd(BASE + struct.unpack('<I', rd(BASE + 0x3C, 4))[0] + 8, 4))[0]
STATIC_POS, CAMERA_SLOT = _BUILDS.get(_pe, (0x71CD0B0, 0x70DA028))  # XY floats (+0), Z (+8); 3527/3549 (3479: 0x71C5DF0)
CAMERA_BLOCK, CAM_FWD = 0x430, 0x90
u32 = ctypes.windll.user32
KEYUP, MOVE = 2, 1


def pos():
    return struct.unpack('<fff', rd(BASE + STATIC_POS, 12))


def cam_yaw():
    cam = q(BASE + CAMERA_SLOT)
    blk = q(cam + CAMERA_BLOCK) if cam else None
    if not blk:
        return None
    fx, fy, fz = struct.unpack('<fff', rd(blk + CAM_FWD, 12))
    return math.degrees(math.atan2(fx, fz))


def focus():
    import subprocess
    out = subprocess.run(['powershell', '-NoProfile', '-Command',
                          ("(Get-Process -Id %s).MainWindowHandle" % os.environ['CD_PID']) if os.environ.get('CD_PID') else
                          "(Get-Process CrimsonDesert | Where-Object {$_.MainWindowHandle -ne 0} | Select-Object -First 1).MainWindowHandle"],
                         capture_output=True, text=True).stdout.strip()
    h = int(out)
    if u32.GetForegroundWindow() != h:
        u32.keybd_event(0x12, 0, 0, 0); u32.keybd_event(0x12, 0, KEYUP, 0)
        u32.SetForegroundWindow(h); time.sleep(0.3)
    return h


def shot(h, path):
    from PIL import ImageGrab
    import ctypes.wintypes as wt
    r = wt.RECT(); u32.GetWindowRect(h, ctypes.byref(r))
    ImageGrab.grab(bbox=(r.left, r.top, r.right, r.bottom), all_screens=True).save(path)


def key(vk, down):
    u32.keybd_event(vk, u32.MapVirtualKeyW(vk, 0), 0 if down else KEYUP, 0)


def turn(dx):
    steps = max(1, abs(int(dx)) // 20)
    for _ in range(steps):
        u32.mouse_event(MOVE, int(dx / steps), 0, 0, 0)
        time.sleep(0.01)


def wrap(a):
    return (a + 540) % 360 - 180


def calibrate():
    """Mouse units per degree of camera yaw, with its sign."""
    y0 = cam_yaw(); turn(200); time.sleep(0.4); y1 = cam_yaw(); turn(-200); time.sleep(0.4)
    d = wrap(y1 - y0) if y0 is not None and y1 is not None else 0
    return 200.0 / d if abs(d) > 1 else 4.0


def aim(tx, tz, gain):
    for _ in range(4):
        x, _, z = pos()
        want = math.degrees(math.atan2(tx - x, tz - z))
        yaw = cam_yaw()
        if yaw is None:  # the camera block is not readable this instant (tc41/tc42: a menu / duel camera): try again
            time.sleep(0.3)
            continue
        err = wrap(want - yaw)
        if abs(err) < 4:
            return
        turn(err * gain); time.sleep(0.25)


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    tx, tz = float(args[0]), float(args[1])
    stop = float(args[2]) if len(args) > 2 else 8.0
    sprint = '--walk' not in sys.argv
    h = focus()
    for _ in range(20):  # wait up to ~6 s for a readable camera before calibrating
        if cam_yaw() is not None:
            break
        time.sleep(0.3)
    gain = calibrate()
    print('gain %.2f mouse units / degree' % gain, flush=True)
    stuck = 0
    for it in range(300):
        if u32.GetForegroundWindow() != h:
            print('lost focus; stopping'); break
        x0, y0, z0 = pos()
        dist = math.hypot(tx - x0, tz - z0)
        if dist < stop:
            print('arrived: %.1f m from target at (%.1f, %.1f, %.1f)' % (dist, x0, y0, z0)); break
        aim(tx, tz, gain)
        key(0x57, True)
        if sprint:
            key(0x10, True)
        time.sleep(0.6 if dist > 20 else 0.3)
        key(0x57, False); key(0x10, False)
        x1, y1, z1 = pos()
        if y0 - y1 > 6:
            print('fell %.0f m at (%.1f, %.1f, %.1f); stopping' % (y0 - y1, x1, y1, z1)); break
        if math.hypot(x1 - x0, z1 - z0) < 0.3:
            stuck += 1
            key(0x20, True); time.sleep(0.1); key(0x20, False)  # jump
            if stuck > 15:
                print('stuck at (%.1f, %.1f, %.1f)' % (x1, y1, z1)); break
        else:
            stuck = 0
        if it % 10 == 0:
            print('at (%.0f, %.0f, %.0f) %.0f m left' % (x1, y1, z1, math.hypot(tx - x1, tz - z1)), flush=True)
    # --face=X,Z (2026-11-08): then turn the camera toward that point (a stall, an NPC) without moving
    face = [a for a in sys.argv[1:] if a.startswith('--face=')]
    if face:
        fx, fz = (float(v) for v in face[0][7:].split(','))
        aim(fx, fz, gain); time.sleep(0.3); aim(fx, fz, gain)
        x, _, z = pos()
        print('facing (%.1f, %.1f) from (%.1f, %.1f), camera yaw %s' % (fx, fz, x, z, cam_yaw()), flush=True)
        # --sweep=FROM,TO,STEP,VK,PREFIX (2026-11-08): turn the camera FROM..TO degrees around that facing in STEP
        # steps; at each, tap the key (virtual-key code, e.g. 82 = R) and save a window shot shots/PREFIX-<n>.png
        sweep = [a for a in sys.argv[1:] if a.startswith('--sweep=')]
        if sweep:
            parts = sweep[0][8:].split(',')
            lo, hi, st, vk, prefix = parts[:5]
            hold = float(parts[5]) / 1000 if len(parts) > 5 else 0.15
            lo, hi, st, vk = float(lo), float(hi), float(st), int(vk)
            base = math.degrees(math.atan2(fx - x, fz - z))
            n, off = 0, lo
            while off <= hi + 0.01:
                a = math.radians(base + off)
                aim(x + 3 * math.sin(a), z + 3 * math.cos(a), gain)
                time.sleep(0.4)
                key(vk, True); time.sleep(hold); key(vk, False)
                time.sleep(0.5)
                shot(h, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'shots',
                                     '%s-%02da.png' % (prefix, n)))
                time.sleep(1.5)
                shot(h, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'shots',
                                     '%s-%02d.png' % (prefix, n)))
                print('sweep %d: yaw %+.0f, key %d' % (n, off, vk), flush=True)
                n += 1; off += st
                # --stopfile=PATH (2026-11-09): stop sweeping once the game wrote it (an interaction opened); 3 more shots
                stop = [x[11:] for x in sys.argv[1:] if x.startswith('--stopfile=')] or                     [r'D:\SteamLibrary\steamapps\common\Crimson Desert\bin64\shop_twin_open.flag']
                if os.path.exists(stop[0]):
                    for k in range(3):
                        time.sleep(2.0)
                        shot(h, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'shots',
                                             '%s-open%d.png' % (prefix, k)))
                    print('stopfile seen: stopped after sweep %d' % (n - 1), flush=True)
                    break


if __name__ == '__main__':
    try:
        main()
    finally:
        key(0x57, False); key(0x10, False)
