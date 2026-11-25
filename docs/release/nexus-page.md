# Nexus Mods page (NOT POSTED: for the release, B22; rewritten 2026-11-22 for the shared world)

Paste-ready text for the mod page. Before posting: update the version lines and "Known issues" from the latest
CHANGELOG and README-shared-world.txt (kit/coopplus), and take the screenshots in `docs/release/shot-list.md`.
Files to upload: the `...-shared-world.zip` (main file) and the `...-shared-world-mod-manager.zip` (optional file),
never a `-symbols-KEEP-PRIVATE.zip`. Posting needs the owner's OK (it publishes ByJunter's GPL code too).

**Name:** Crimson Desert Seamless Co-op+
**Category:** Gameplay. **Tags:** multiplayer, co-op, gameplay. **Version:** 0.1.0-alpha (shared world).
**Game version:** 1.0.0.3732 (Steam). On any other version the mod switches itself off and the game runs normally.

## Summary (one line, at most 350 characters)
Play Crimson Desert with a Steam friend in one shared world: your friend joins your actual world with their own
character. Same townsfolk, same enemies, your own stories, challenges, trading and revives. Your own saves are never
written in co-op.

## Description

Crimson Desert Seamless Co-op+ is an unofficial fan mod that brings two-player co-op to the game. The host's game runs
the world and the friend plays in it with their own character, gear and progress: the same townsfolk, merchants and
enemies for both of you.

It is built on Crimson Desert Multiplayer by ByJunter (its NativeServer shared world) and its port to newer game
versions by nouzert1. Co-op+ adds Steam joining from the title screen, co-op saves per friend, the shared-world fixes
and the co-op features below.

**This is an alpha.** Read "Known issues". Co-op plays in its own co-op save; the installer backs up your saves anyway.

### Features
- **Join from the title screen:** the Co-op menu stands in for the title. Invite a Steam friend, then Start Co-op:
  Continue Our Co-op Save, Start From My Solo Progress, or New Co-op Save. Both games restart once into co-op. No
  Steam? "Radmin VPN / Direct IP" joins over a VPN or LAN with a one-time invite.
- **One shared world:** the same townsfolk and merchants for both. The friend talks to them, buys and sells with their
  own money and stock, also far from the host. Doors and buildings work for whoever gets there first.
- **Shared enemies:** both of you hit and kill the same enemies; an enemy fights the player who fights it. Enemy
  health scales with a guest (configurable). No friendly fire.
- **Items for each of you:** each player can take each plant, ore vein, chest and body's loot once ("each their own").
  A chest your friend opened keeps your share for you, pinned on your map.
- **Your own story:** each of you keeps and plays your own quests in the shared world. Optional: shared quest credit
  (a step you are both on counts for both) and story scenes that wait for both of you.
- **Mounts:** each sees the other ride (own horse, a borrowed or wild one, Blackstar).
- **Revive each other:** the game's own revives, plus 30 seconds in which your friend can get you up where you fell
  by holding Interact by you. At a boss you can wait for your friend.
- **Challenges:** hold Interact on your friend for duels (a ring, rounds, nobody dies), rock-paper-scissors, a hunting
  contest, a race to the map waypoint, hide and seek and a fishing contest. Stake items on a challenge.
- **Giving and trading:** give an item, offer a trade, or put it in the shared stash (the host's warehouse).
- **Co-op tab** in the game's pause menu: session, travel to / call your friend, quick messages with map pins, the
  settings, controls and status. Chat with Shift+F2.
- **Crimes are your own:** what you do makes you wanted, not your friend.
- **Time of day and weather** follow the host.
- **Safety:** your own saves are never written in co-op (a co-op save per friend); version check at the menu;
  automatic resync; your friend can't kill your quest NPCs; the game runs normally after a game update until Co-op+ is
  updated.
- **Controller support** throughout. **One-click** install, uninstall, vanilla mode and a diagnose report.

### Requirements
- Crimson Desert on Steam, game version 1.0.0.3732. Both players need the **same Co-op+ zip**.
- Steam friends, with the Steam overlay on (or Radmin VPN / a LAN for Direct Play).
- Ultimate ASI Loader is included (ThirteenAG, MIT). Nothing else to install.

### Installation
1. Close the game. Unzip the main file anywhere.
2. Double-click `Install Co-op+.cmd`. It finds the game through Steam, backs up your saves to
   Documents\CrimsonDesert-CoopPlus-SaveBackups and puts its files into the game's bin64 folder, keeping any file it
   replaces.
3. Start the game from Steam. The Co-op menu stands in for the title screen.

**Uninstall:** `Uninstall Co-op+.cmd` removes exactly what it installed and restores anything it replaced.
**Vanilla mode:** `Vanilla mode on-off.cmd` switches the mod off and on, keeping your settings.
**Manual / Vortex / MO2:** use the optional `-mod-manager.zip`. Its `bin64` folder goes into the game folder (next to
CrimsonDesert.exe). Back up your saves yourself.

### Controls
Co-op+ adds no hotkeys in the shared world except Chat (Shift+F2): hold the game's Interact (R; pad A) on your friend
for the challenges and trading menu, and use the Co-op tab in the pause menu for everything else. Optional key bindings
can be added in coopplus.ini (the README explains how).

### Known issues
- Joining only from the title screen, not during play. Two players only.
- Arm wrestling and the game's other minigames against each other: not yet.
- Mining with the hand drill and fishing as the joined player have problems we have not fixed yet.
- The host's armour dye does not show on the joined player's screen yet.
- A tree the joined player cuts down can show twice for the host.
- Taming a wild horse as the joined player: a fix is in, not yet confirmed on a real taming (if the last step still
  fails, they are freed a few seconds later and the horse is most likely not tamed).
- Arm wrestling in solo with Co-op+ installed was reported once to leave the character jumping until a restart.
- While the host is in a conversation (or a scene), the joined player cannot start a talk with any NPC; it works again
  once the host is done.
- Story scenes that start from a talk do not wait for both of you yet.
- Boss fights are not tested with two players yet.
- After a game update Co-op+ switches itself off until it is updated.
- (Update from the latest playtest reports before posting.)

### Other mods
Most mods work alongside Co-op+. In the shared world the host's game runs the world's rules, so gameplay mods
(stamina, stacks, trust, vendors, cooldowns) must be the same on both PCs; cosmetic and interface mods only change your
own screen. Keep only one ASI loader. See OTHER-MODS.md in the download.

### Is it safe?
Crimson Desert has no anti-cheat and no online play of its own. Co-op+ changes only your local game, never your
account, the store, cross-save or Steam Cloud. It sends nothing anywhere except to the friend you play with. If an
update ever adds an anti-cheat, Co-op+ installs nothing and tells you.

### Bug reports
Use the Bugs tab with BUG-REPORT.txt from the download. Attach the zip "Diagnose Co-op+.cmd" puts on your Desktop,
from both players if you can (each game knows only its own side).

### Credits and permissions
- **ByJunter**: Crimson Desert Multiplayer (GPL-3.0), the NativeServer shared world and its research.
  https://www.nexusmods.com/crimsondesert/mods/3677
- **nouzert1**: cd-coop-3446 (GPL-3.0), the port to newer game versions.
- **Plitos**: playtest reports, merged fixes and the save-protection design. **Borque** and the other playtesters:
  reports, videos and logs.
- **ThirteenAG**: Ultimate ASI Loader (MIT), included as winmm.dll.
- Libraries: Dear ImGui (Omar Cornut, MIT), SafetyHook (cursey, BSL-1.0), Zydis / Zycore (MIT), toml++ (Mark
  Gillard, MIT).
- No other authors' mod files are included. Mods that inspired features are credited in CREDITS.md.
- Source code (GPL-3.0) of exactly the build is in the download: source.zip (Co-op+) and source-native.zip (the
  shared-world base).
- Not affiliated with or endorsed by Pearl Abyss. Crimson Desert is a trademark of Pearl Abyss Corp.

### Permissions (for the Nexus "Permissions and credits" tab)
- Upload elsewhere: no (link to this page).
- Modify and reupload: yes, under GPL-3.0 with credit and source.
- Assets: GPL-3.0 code, with credit.
- Use in paid mods: no. Donation points: no.
