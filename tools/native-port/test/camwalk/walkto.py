# Walk our character to a world position (x, z): steer the camera toward the target (camera forward read
# from memory, read-only), hold W (+Shift) in short bursts, stop on arrival, a fall or being stuck.
#   python walkto.py X Z [stop_m] [--walk]
#   (native two-copy harness: set CD_PID to the copy to drive; live.py reads that process)
import ctypes, math, os, struct, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from live import BASE, rd, q

# Per game build, from the running game's PE timestamp (2026-11-06: 1.0.0.3576 moved both; 3527 and 3549 share theirs).
_BUILDS = {0x6AED2E85: (0x71CF140, 0x70DC130),  # 3576 (build/port3576/map_3576.json)
           0x6AF5D198: (0x7253E60, 0x71602A8)}  # 3663 (tools/native-port/p63.py: 2 of 2 code references each)
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
        err = wrap(want - cam_yaw())
        if abs(err) < 4:
            return
        turn(err * gain); time.sleep(0.25)


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    tx, tz = float(args[0]), float(args[1])
    stop = float(args[2]) if len(args) > 2 else 8.0
    sprint = '--walk' not in sys.argv
    h = focus()
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


if __name__ == '__main__':
    try:
        main()
    finally:
        key(0x57, False); key(0x10, False)
