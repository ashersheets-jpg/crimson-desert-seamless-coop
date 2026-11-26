# Port 3732 -> 3840 (2026-11-26): the leftovers in files no helper owned (hand/G3_applied.md, hand/G4.md). ONE SHOT.
#  ui_pause.cpp: the client fast travel is CALLED: 0x74E830 is mid-function in 3840; the same function as fast_travel's
#    kFastTravel 0x787650 (G3: its 3 callers pair with 3732's, the same argument set-up; coordinator: the context chain
#    [[[0x73F0DA8]+0x90]+0x1240] read the same way at 0x100276F, build/port3840/hand/uipause84b.py).
#  remote_interp_logic.h: the movement controller's sample buffers +0x430 -> +0x480, +0x4E8 -> +0x538 (G3: Update-
#    Interpolation 0x9E4930 `lea rbx,[rcx+0x480]`; G4: the movement step 0xC41380 `lea rbx,[rcx+0x538]`).
#  weather_sync_logic.h / clock_push.cpp: the scene's weather pointer +0xF70 -> +0xF48 (G3: all 31 scene-getter readers).
import pathlib

S = pathlib.Path('C:/Users/asher/CrimsonDesert-SeamlessCoop/upstream/cdmp-native/src/mod/session2')


def sub(name, pairs):
    p = S / name
    s = p.read_bytes().decode('utf-8')
    for a, b in pairs:
        assert s.count(a) == 1, (name, a, s.count(a))
        s = s.replace(a, b)
    p.write_bytes(s.encode('utf-8'))
    print('  ', name)


sub('ui_pause.cpp', [
    ('constexpr uintptr_t kFastTravelEntry = 0x74E830;',
     'constexpr uintptr_t kFastTravelEntry = 0x787650;  // 1.0.0.3840 (3732 0x74E830; the same function as fast_travel.cpp kFastTravel)'),
    ('"Co-op+ travel: the client\'s fast travel 0x74E830 to actor', '"Co-op+ travel: the client\'s fast travel 0x787650 to actor'),
])
sub('remote_interp_logic.h', [
    ('inline constexpr uint32_t kPosBuffer = 0x430;  // mc + 0x430 on 3549',
     'inline constexpr uint32_t kPosBuffer = 0x480;  // 3840 (UpdateInterpolation 0x9E4930: lea rbx,[rcx+0x480]); mc + 0x430 on 3549..3732'),
    ('inline constexpr uint32_t kRotBuffer = 0x4E8;  // mc + 0x4E8 on 3549',
     'inline constexpr uint32_t kRotBuffer = 0x538;  // 3840 (the movement step 0xC41380: lea rbx,[rcx+0x538]); mc + 0x4E8 on 3549..3732'),
])
sub('weather_sync_logic.h', [
    ('inline constexpr uint32_t kSceneWeather = 0xF70;     // X = [S+0xF70] (2976 +0xF68)',
     'inline constexpr uint32_t kSceneWeather = 0xF48;     // X = [S+0xF48] (3840: the 31 scene-getter readers; 3732 +0xF70, 2976 +0xF68)'),
])
sub('clock_push.cpp', [
    ('const auto px = sc ? game::read_ptr(sc + 0xF70) : std::nullopt;',
     'const auto px = sc ? game::read_ptr(sc + 0xF48) : std::nullopt;  // 3840 (3732 +0xF70): weather_sync_logic.h kSceneWeather'),
])
print('leftovers: done')
