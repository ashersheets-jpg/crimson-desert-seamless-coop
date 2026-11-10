# Crimson Desert Seamless Co-op+ (playtest)

An unofficial fan mod that adds two-player co-op to **Crimson Desert** (Steam). Each player keeps their own save;
your friend joins your world as their own character. It is built on Crimson Desert Multiplayer by ByJunter and its
port by nouzert1. GPL-3.0, and the source of each build is inside its zip (`source.zip`, and `source-native.zip`
in the shared world builds).

**Alpha playtest.** Expect bugs. The installer backs up your saves.

## Download
Get the newest build from **[Releases > Latest](../../releases/latest)**. Download the file named
`CrimsonDesert-SeamlessCoop-<version>-shared-world.zip` (not the `-mod-manager` one, unless you use a mod
manager).

**Both players need the same zip.**

## Requirements
- Crimson Desert on Steam, game version **1.0.0.3576**. On another version the mod switches itself off.
- You and your friend are Steam friends, with the Steam overlay on.

## Install
1. Close the game.
2. Right-click the downloaded zip > **Properties**. If there's an **Unblock** box at the bottom, tick it and press
   OK. Windows marks downloaded files, and this stops the "Windows protected your PC" prompt.
3. Right-click the zip > **Extract All...** and extract it anywhere, for example the Desktop.
4. In the extracted folder, double-click **`Install Co-op+.cmd`**. It finds the game through Steam, backs up your
   saves to `Documents\CrimsonDesert-CoopPlus-SaveBackups`, and copies the mod into the game's `bin64` folder.
   - If Windows SmartScreen still appears: **More info > Run anyway**. The scripts are plain text, so you can open
     them in Notepad first.
5. Start Crimson Desert from Steam: the Co-op menu appears in place of the title screen. A controls card appears the first time. **Shift+F1** (or hold
   View / Touchpad and click the left stick) opens it again any time.

## Play together (shared world builds)
Both of you stay at the title screen; don't load a save.
1. **Host:** pick **Invite Friends** in the Co-op menu and choose your friend in the Steam overlay.
2. **Friend:** accept the invite, or right-click the host in your Steam friends list > **Join Game**.
3. **Host:** pick **Start Co-op** and choose how the journey begins. Next time, pick **Continue Our Co-op Save**.
4. Both games restart once into the shared world (about 30 seconds).

- Your friend joins your actual world: the same NPCs, merchants and enemies for both of you.
- Inviting and joining only work from the title screen. If you are already in a world, go back to the title
  screen first.
- **Co-op has its own save folder, one per friend. Your normal saves are never changed by Co-op+.**
- **F8** (pad: View + X) opens the co-op panel: connection, settings, your record.

## Uninstall / turn off
- `Uninstall Co-op+.cmd` removes exactly what was installed.
- `Vanilla mode on-off.cmd` switches the mod off and back on, keeping your settings.

## Report a bug
Double-click `Diagnose Co-op+.cmd` (game closed). It puts a zip on your clipboard with your Steam names, IDs and
Windows user name removed. Paste it in the Discord bug channel with what happened (`BUG-REPORT.txt` has the
questions). Reports from **both** players are best.

## What's in it
See [FEATURES.md](FEATURES.md) (written for the builds before the shared world; being updated). Features marked **Needs testing** are the ones that most need your reports.

Not affiliated with or endorsed by Pearl Abyss. Crimson Desert is a trademark of Pearl Abyss Corp.
