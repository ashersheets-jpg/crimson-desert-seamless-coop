CRIMSON DESERT SEAMLESS CO-OP+  (alpha, @VERSION@)
===================================================

Play Crimson Desert with a Steam friend. Unofficial fan mod; not made or endorsed
by Pearl Abyss. ALPHA: expect rough edges, and back up your saves (the installer
does it for you).

Built on "Crimson Desert Multiplayer" by ByJunter and its port to the current game
version by nouzert1. See CREDITS.md.


BEFORE YOU START
----------------
- Both players: Crimson Desert on Steam, game version 1.0.0.3663 (the current
  patch when this was built). On another version the mod switches itself off and
  the game runs normally.
- Both players need THIS SAME ZIP. Different builds warn "Different mod versions".
- You must be Steam friends.
- Steam overlay on (Steam > Settings > In Game > Enable the Steam Overlay).


INSTALL (each player, game closed)
---------------------------------
1. Unzip this folder anywhere (Desktop is fine).
2. Double-click  "Install Co-op+.cmd".
   It finds the game through Steam, copies your saves to
   Documents\CrimsonDesert-CoopPlus-SaveBackups, and puts three files into the
   game's bin64 folder: winmm.dll (Ultimate ASI Loader), cdmp.asi (the mod) and
   cdmp.ini (settings).
3. Start the game from Steam as usual.

What the installer changes: only the game's bin64 folder (the files listed in
bin64\coopplus-installed.txt). Anything it would replace is kept as *.before-coopplus.

To remove it: double-click "Uninstall Co-op+.cmd" (game closed). It removes exactly
the files it installed and puts back any *.before-coopplus file.
To play without it for a while: "Vanilla mode on-off.cmd" switches Co-op+ off and on
again, keeping your settings.
Something wrong? "Diagnose Co-op+.cmd" writes a report and a .zip (report + log) to
your Desktop and puts the .zip on the clipboard: paste it (Ctrl+V) into a message.
Steam names and ids, your Windows user and PC name and IP addresses are taken out.


MANUAL INSTALL / VORTEX / MOD ORGANIZER (instead of the installer)
------------------------------------------------------------------
Use the "...-mod-manager.zip" download. It has one folder, bin64, holding exactly the
files the installer would copy: winmm.dll, cdmp.asi, cdmp.ini, cdcoop\config\scaling.toml
and the Co-op+ readme / licence files.
- By hand: back up %LOCALAPPDATA%\Pearl Abyss\CD\save yourself first (the manual way
  makes no backup). Then copy the bin64 folder from the zip onto the game folder
  (...\steamapps\common\Crimson Desert), so the files land next to CrimsonDesert.exe.
  If the game folder already has a winmm.dll from another mod, it is the same kind of
  ASI loader; keeping one is enough (see OTHER-MODS.md).
  To remove: delete those files from bin64.
- Vortex / MO2: add the zip as a mod that deploys into the GAME folder (not into a
  data or archive folder), so bin64\cdmp.asi ends up next to CrimsonDesert.exe.
  Vortex has no Crimson Desert support of its own that we have tested; if it asks,
  pick the game's root folder as the target. Disable the mod in the manager to remove it.
- Either way: both players must use the same Co-op+ version.


OTHER MODS
----------
Most other mods work alongside Co-op+. Two rules: gameplay mods (stamina, stacks,
loot, cooldowns) must be the same on both PCs, and keep only ONE ASI loader
(Co-op+'s winmm.dll loads every .asi in bin64). When the game starts, Co-op+ names
other loaders, .asi mods and ReShade it finds next to the game, once, as a notice.
It never blocks them. Details and a table by kind of mod: OTHER-MODS.md.


QUICK START (the first time, about 2 minutes)
---------------------------------------------
1. Both players: install (above), then start the game from Steam. The Co-op menu
   stands in for the game's title screen.
2. Host: pick "Invite Friends". The Steam overlay opens: pick your friend.
3. Friend: accept the invite in Steam (the pop-up, or right-click the host in the
   friends list > "Join Game"). Stay at the Co-op menu. Both menus now list the
   party top right; the crown marks the host.
4. Host: pick "Start Co-op", then how the journey begins:
   - Continue Our Co-op Save: where the two of you left off (greyed until you have one).
   - Start From My Solo Progress: your friend joins your story where you are.
   - New Co-op Save: both begin the story; the host picks the difficulty, the friend
     sees "waiting for the host" while the mod sets their game up the same way.
   A NEW GAME starts with the prologue (the opening tutorial). Each of you plays it on
   your own: it breaks when played together. You can still chat; co-op starts by
   itself once the prologue is behind you (at the latest on your first ride).
   (With co-op saves off in the settings there is one choice: "Play Together", each in
   their own save.)
5. Both games start together. Play alone instead: "Start Solo" (or Esc).
Something went wrong (a crash, a stuck screen)? Run "Diagnose Co-op+.cmd" and send the
.zip it puts on your Desktop; it holds the log we need.
No Steam invite possible (Radmin VPN / LAN)? See DIRECT PLAY below.


PLAYING TOGETHER
----------------
- Everyone starts as the host of their own world (QUICK START above shows the menu).
- CONTROLLER: every co-op action works on an Xbox or PlayStation pad. Hold View
  (Xbox) / the touchpad button (PlayStation) and press a button; the on-screen
  prompts show the buttons of whatever you last used (keys, Xbox or PlayStation).
    Keyboard    Pad (hold View/Touchpad +)   Action
    Ctrl+F1     Y / Triangle                 invite a friend (Steam overlay)
    Ctrl+F2     B / Circle                   leave (guest): back to your own world
    Ctrl+F3     D-pad Left                   quick messages (Go here, Danger, Loot here, Follow me...)
    Shift+F2    -                            chat: type a line, Enter sends, Esc cancels
    Ctrl+F4     D-pad Right                  go to your friend
    Shift+F5    D-pad Up                     challenge / accept / end a challenge
    Shift+F6    D-pad Down                   decline / cancel
    F8          X / Square                   co-op panel
    Ctrl+F8     A / Cross                    your friend's name over their head
    Shift+F8    RB / R1                      resync (if your friend or the enemies look wrong)
    Shift+F7    LB / L1 (hold)               revive your friend when they are down (stand by them)
    Shift+F4    right stick click            ask your friend to come to you (they accept with Ctrl+F4)
    Shift+F1    left stick click             the controls card (shown once on the first start)
  A quick tap on View still does what it does in the game.
- Host: press Ctrl+F1 to open the Steam overlay and invite your friend.
- Friend: accept the invite, or right-click the host in the Steam friends list and
  choose "Join Game". If the friend's game is closed, Steam starts it and the mod
  joins as soon as they load a save.
- The friend appears in your world as a character, on the minimap and the map.
- Their name, health and stamina stay on the left of your screen (above the date and
  place); it pulses red and says "Badly hurt" when they are low.
- Set a waypoint on your map (right-click) and your friend sees it on theirs
  as a second pin.
- Ctrl+F2 (guest): leave and go back to your own world.
- F8: show/hide the co-op panel (top left): connection, ping, packet loss (green /
  yellow / red), room code, and the
  co-op SETTINGS: Up/Down arrows (pad: D-pad) pick one, Left/Right change it. Saved
  at once in cdmp.ini; most apply at once, "(next start)" ones after a restart. Who
  can join, loot of shared kills, revive, respawn, weather, how much tougher
  enemies and bosses get with a guest (x1.0 to x3.0; the host's values count), and
  "Ping and packet loss on screen" (a small readout, top right), "Player colours"
  (the host is gold with a diamond mark, the guest blue with a round mark; or a
  colour-blind-safe orange / sky blue), your friend's map pin colour, and "My
  friend's story": hidden = no spoilers (their quest steps you have not reached are
  not named, and their story scenes are not played in your game).
- Different keys: in bin64\cdmp.ini add a [keys] section, one line per action, e.g.
    [keys]
    Invite=Ctrl+G
    Panel=F9
    Summon_pad=L3
  Actions: Invite Leave Ping GoToFriend Challenge Decline Panel Nameplates Resync
  ReviveFriend Summon Chat Help. Keys: A-Z, 0-9, F1-F12, Esc Tab Space Enter Insert
  Delete Home End PageUp PageDown arrows, with Ctrl+ and/or Shift+. Pad (_pad):
  A B X Y LB RB L3 R3 Up Down Left Right (always with View/Touchpad held). When
  the game starts, a notice warns about two actions on the same keys, or a key
  with no Ctrl/Shift that the game probably uses too.
- Ctrl+F8: show/hide your friend's name over their head.
- Ctrl+F3 (pad: View + D-pad Left): quick messages -- Go here, Danger here!, Loot
  here, Follow me, Wait for me, Help!, Thanks!, Ready? -- arrows / D-pad Left-Right
  choose, Ctrl+F3 again sends; the three "here" ones also put a pin on your friend's map.
- Group photos: the game's photo mode (pause menu, beta) shows your friend too. Send
  the quick message "Hold still: photo!" (Ctrl+F3, last in the list) so they stop and
  pose; Co-op+'s own labels hide while a game menu is open.
- Shift+F2: chat. Type a line (the game does not see your keys meanwhile), Enter sends,
  Esc cancels. Messages show as a notice and in a short log on the left.
- Ctrl+F4: go to your friend (you are moved next to them in short hops; works best
  within a few hundred metres; it stops if you are swimming).
- Shift+F4: ask your friend to come to you. They see "<name> asks you to come to
  them" and go with their Ctrl+F4 (or let it lapse); once every 30 seconds. When one
  of you fast travels, the other is offered to follow the same way.
- Death in co-op: the game's own death screen ("Death has found you."): Q / X
  instant revive (30% health), U / Y revive at the checkpoint. For 30 seconds your
  friend can also get you up where you fell: they stand by you and hold Shift+F7
  (pad: View + LB) for 3 seconds. ([coop] teammate_revive=0 turns this off.)
  [coop] respawn_at=partner: after a revive at the checkpoint you are taken back to
  your friend (default: checkpoint, as in the game). Death costs what the game
  itself takes; Co-op+ adds no penalty.
- Cutscenes: when your friend is watching one near you (within 100 m), you see
  "<name> is watching a cutscene". When the host's cutscene belongs to a boss or
  story scene, the guest's game starts the same scene too (new, not yet tried by
  two players). A shared cutscene is skipped only when EVERYONE in it pressed Esc
  (pad: Menu/Options or B): yours says "waiting for <name>" until they press too.
- Time of day and weather follow the host (rain, clouds and wind included).
- CHALLENGE YOUR FRIEND (Shift+F5, pad: View + D-pad Up):
  * A choice opens: Duel, Rock-paper-scissors, Hunting contest, Race, Hide and seek, Fishing
    contest, and the arm / hand wrestling table you stand at (if any). Left/Right arrows (pad: D-pad Left/Right) choose,
    Shift+F5 sends, Shift+F6 cancels.
  * Your friend gets "<name> challenges you to ...": Shift+F5 accepts, Shift+F6
    declines (an invite lapses after a minute).
  * DUEL: a 1v1 fight. A gold ring (20 m) marks the duel ground; 3, 2, 1, Fight!
    Your friend's health shows at the top. Bring them down to win the round
    (nobody dies: the loser kneels at 3% health). Leave the ring for 10 seconds and
    you lose the round. After each round both are healed and the next one starts.
  * HUNTING CONTEST: 3 minutes to bring down the most game (bow or blade). Small
    game scores 1, deer and foxes 2, bears, wolves and boars 3. Each of you hunts
    the animals of your OWN world (see below): the herds are in the same places for
    both of you, but an animal your friend chases is theirs, not one you can shoot.
  * Arm / hand wrestling: both of you sit at the table and start a match; your
    friend's character sits across from you in the opponent's chair, and you share
    one strength bar: whoever mashes E (pad: any face button) faster wins on both
    screens.
  * Rematch: after a challenge ends, Shift+F5 opens the picker on the game you played
    last, so Shift+F5 twice asks for a rematch. The duel ground's size (12 / 20 / 35 m)
    is in the F8 settings; the one who places the ground decides.
  * SCOREBOARD: every round you win or lose is kept per friend (bin64\cdmp_scores.ini,
    kept by Uninstall); F8 shows "Record vs <friend>". Wins earn a title your friend
    sees after your name: the Sparring Partner (3 wins), the Champion (25), the
    Duelist / the Swift / the Huntmaster (10 duels / races / hunts won).
  * RACE TO THE WAYPOINT: the host's map waypoint (right-click on the map) is the
    finish; after 3, 2, 1 the first to get within 12 m wins (ride, glide, run). Set a
    new waypoint for the next race.
  * HIDE AND SEEK: the one who sent the challenge counts first (eyes closed: the
    screen goes dark for 45 seconds) while the other hides within 100 m of where they
    count; a gold ring on the ground marks the edge. Then the seeker has 3 minutes:
    the hider's name tag, map marker and distance are hidden, and "Cold / Cool / Warm /
    Hot" says how close you are. Get within 4 m to find them. Not found in time: the
    hider wins the round. A hider outside the ring for 10 seconds loses it. Roles swap
    every round.
  * FISHING CONTEST: six minutes, each of you fishing in your own world; every fish you
    keep (F on the catch card) scores: river fish 1, sea fish 2, legendary fish 5.
    Released fish score nothing. Your friend sees what you caught. A draw plays another
    round. (Fishing in the game: put the rod in the ranged slot with 2, take it out with
    Ctrl, hold the left button and aim until the marker turns blue, release to cast, press
    the right button when a fish bites, steer A/D against it and reel with the wheel.)
  * Rock, paper, scissors: press U (scissors), F (paper) or E (rock) - pad X, Y,
    B; both hands show over your characters when both of you have picked; first to
    2 wins the match.
  * Each challenge is one match: duel, race, hunt and hide and seek are first to 2
    rounds, the fishing contest one round; it ends with a Victory or Defeat card. Shift+F5 stops it early.
- WHEN YOU PART WAYS: once your friend has left (and not come back for 20 seconds), a
  "Journey Together" card sums up your session: time together, enemies each of you
  defeated, bosses, revives, challenges won, quest steps finished together and the
  distance travelled side by side (sessions of 2 minutes or more; Shift+F6 closes it).
- Other minigames (contests, card games...): each of you plays in your own world;
  your friend sees "<name> is playing a minigame" and when you finish.
- Mounts: your friend sees you ride your horse, and sees your flying mount too.
- Menus: opening the map, inventory or pause menu pauses only YOUR world. Meanwhile
  your character stands still in your friend's world, their enemies leave it
  alone, nothing can hurt you, and your name tag on their screen says "in a menu".

Privacy: by default any Steam friend who plays with Co-op+ can "Join Game" into your
world. To allow only friends you invite, open the game's bin64\cdmp.ini and set
join=invite under [net].

Fallback: the panel shows a 7-letter ROOM CODE for the host. A friend can type it
into cdmp.ini under [room] code=  and restart the game.


CO-OP SAVES + LIVE QUESTS (EXPERIMENTAL - you choose)
-----------------------------------------------------
The first time you start the game with this version, a card asks whether to turn this
on. Shift+F5 (pad: hold View + D-pad Up) turns it on, Shift+F6 (View + D-pad Down) is
"not now". Change it any time: F8 > Settings > "EXPERIMENTAL: co-op saves + live
quests". BOTH players need it on.

When it is on:
- Each friend you play with gets a co-op save of its own, for the host and the guest,
  like Elden Ring Seamless Co-op. Your own saves are never changed.
- The first time you play together, you both begin a NEW JOURNEY: a card asks
  ("A New Journey with <friend>": begin now, or later from the title screen), the game
  returns to the title and starts a new game; each of you accepts the agreement and
  picks a difficulty. From then on, playing together loads that co-op save where you
  left off, and when the session ends you go back to your own saves (asked first too).
- A co-op save made by an earlier version (a copy of your own progress): the host is
  asked once whether to begin a new journey (the old co-op save is kept as a backup
  folder) or to continue it.
- Quest steps follow between you live: when your friend finishes a step that you also
  have under way (go somewhere, defeat enemies, talk to someone), it finishes for you
  too, with its rewards, and you get a notice. Steps you have not started are left
  alone.
- The co-op saves are in %LOCALAPPDATA%\CrimsonDesert-CoopPlus\Worlds (outside Steam
  Cloud).
It is new and has only been tested on one PC: please tell us in the Discord how it goes.
When it is off, Co-op+ works exactly as before: each of you plays in your own save
and keeps your own quests. Co-op+ never changes your own saves either way.


WHAT WORKS / WHAT DOESN'T (YET)
-------------------------------
- Each player plays in THEIR OWN save and sees the other walking, running,
  climbing, gliding, fighting and riding, with matching gear (gear changes show
  live).
- Enemies near the host are shared: the guest sees the host's enemies, and the
  guest's own enemies there are hidden (they come back when you are apart). Both
  players can hit and kill them; enemies attack whichever of you they choose
  (usually whoever hit them), and their blows hurt either player for real.
- With a guest in, the host's enemies are tougher (about 1.5x health, bosses 1.7x;
  set in bin64\cdcoop\config\scaling.toml, up to 4 players). Their health bar dips and
  partly refills after each blow: that is the extra toughness. Off: enabled=0
  under [scaling] in cdmp.ini.
- No friendly fire: you cannot hurt each other.
- Kill feed: when either of you takes down an enemy or an animal, a short line on the
  right says so ("<name> defeated a Deer"), your friend's in their colour.
- Your friend cannot harm your world: they never touch your townsfolk, items or
  story objects (each of you plays in your own world), and when their blow finishes
  one of your enemies it counts as YOUR kill (your quest, your loot). Bosses are
  always finished by the host. Strict mode, [coop] guest_finishes=0: the guest's
  blows never finish any of the host's enemies (they stop at 1 HP for the host).
- Deaths: each of you dies and revives in your own world; the other keeps playing.
  Once your friend's character has been in your world, the game's own death screen
  no longer appears, so Co-op+ shows the same choice in its place ("Death has found
  you": Q = Instant Revive, U = Revive at Checkpoint). The checkpoint revive moves you
  there without a loading screen.
- Loot is per player: chests and items in the world are in each player's own
  world, so you each get your own. Loot from enemies you kill together: the host
  loots normally; for the guest it is new and still being tested.
- Bosses: the guest helps, the host lands the final blow (protects the host's
  quest). If you die during a boss fight you wait ("Your partner is still
  fighting the boss"): when your partner wins you get up where you fell; if you
  both go down, you both revive at the checkpoint and try again. To turn the
  wait off: boss_death_wait=0 under [coop] in cdmp.ini.
- Who sees what: ENEMIES near the host are shared (above). Animals and townsfolk are
  not: each of you sees the animals and people of your own world. They stand in the
  same places for both (same world), but they are not the same ones: a deer you
  hunt is only in your world, and your friend's deer is only in theirs.
- World events (reinforcements arriving, bounty hunters, camp and outpost activity)
  happen in the world they start in; their fighters are enemies, so the guest sees
  and fights them with the host. Either of you gets a notice when one starts near the
  other ("World event near <name>: Bounty hunters ...").
- Crimes and bounties are your own: what you steal or whom you hit in your world
  makes YOU wanted there, never your friend (and the other way round). When one of
  you becomes wanted, the other gets a notice.
- Slow motion: while you play together, Focus does not slow the world (each of you
  runs your own world, so one player's slow motion would put them out of step). Focus
  still works otherwise. Playing alone it is as always. F8 > Settings: "Slow motion
  (Focus) in co-op" turns it back on.
- Shared: time of day and weather (the host's). Quest steps: shared live with the
  experimental co-op saves on (see above); otherwise each keeps their own quests and
  you get a notice when your friend completes a quest step. Not shared: civilians.
- Two players at a time.

WHAT STAYS AFTER A SESSION
- The host keeps what happened in the host's world: enemies you killed together stay
  dead there (camps cleared, quest credit, loot), bosses are the host's.
- The guest keeps only what happened in the guest's own world (its own chests,
  gathering, quests). The guest's own enemies near the host were only hidden and come
  back when the session ends. With shared loot on, the one enemy of the guest's that
  was killed to give the guest its drop stays dead in the guest's world.
- Time of day, weather and party scaling go back to normal when the session ends.

KNOWN ISSUES (alpha)
- Loot and XP: the guest gets loot from shared kills, but no XP for them yet.
- Arm wrestling table: when the guest arrives at the pub while already connected,
  the table can be empty for them. Leave and come back after your friend is in.
- No trading or gifting items between players, and no death penalty options.
- Doors, levers and thrown or broken objects are each player's own (not synced).
- Quest sharing needs the experimental co-op saves (see above), still untested between
  two PCs. Steps of other kinds (using an object, reading, cutscenes) are untested.
- Bosses: the host always lands the final blow.
- Not yet tried by two real players: shared boss cutscenes, world event notices,
  teammate revive, summoning, the guest's loot from shared kills. Please report
  what you see (BUG-REPORT.txt).
- After a game update Co-op+ switches itself off until it is updated.
- Dying in co-op: Co-op+ shows its own death screen (the game's does not come once your
  friend has joined), and your character stays on its feet behind it until you revive.
- Rock, paper, scissors has no hand animation (it is Co-op+'s own challenge).

IMPORTANT: do not save while the other player is in your world. Quit without
saving, or wait until they leave. Saving during a session is the most likely way to
damage a save. As a safety net the mod copies your saves to
Documents\CrimsonDesert-CoopPlus-SaveBackups (folders named auto-...) each time a
co-op session starts, keeps the newest 10, and tells you on the panel if the game
saved during co-op (the game also autosaves on its own). To restore one: game closed,
copy that folder's contents back into %LOCALAPPDATA%\Pearl Abyss\CD\save.


IS IT SAFE? (ANTI-CHEAT, ACCOUNTS, CROSS-SAVE)
---------------------------------------------
- Crimson Desert has no anti-cheat. Its Denuvo is copy protection only, and the game
  has no online play of its own. Co-op+ changes only your local game.
- Should an update ever add an anti-cheat, Co-op+ notices at start, installs nothing
  at all and tells you in a small window; the game runs normally.
- Co-op+ never touches your account, the store, cross-save or Steam Cloud, and sends
  nothing anywhere except to the friend you play with. It never changes your saves.
- Cross-save / Steam Cloud upload your save when the game saves: that is one more
  reason not to save while your friend is connected.

DIRECT PLAY (Radmin VPN, LAN, any VPN; no Steam invite)
-------------------------------------------------------
Steam invites stay the normal way. Without Steam (or when invites fail):
1. Both players join the same Radmin VPN network (or the same LAN).
2. Host: in the Co-op menu pick "Radmin VPN / Direct IP", then "Host". An invite is
   copied for you (it looks like coopplus1/26.x.x.x:37015/...). Paste it to your
   friend (Discord, any chat). Allow the game on the network if Windows asks.
3. Friend: copy the invite, then in the Co-op menu pick "Radmin VPN / Direct IP",
   then "Join". You land in the host's party; the host starts co-op as usual.
Each invite holds a random secret that changes on every Host, so only the person
you sent it to can join. The mod listens only on the VPN / LAN adapter.
"Back to Steam Invite" (or Leave) returns to Steam. Instead of an invite, a bare
address works too if both copy each other's (Radmin's Copy IP) before Host / Join.
Fixed setup without the menu, in bin64\cdmp.ini under [net]:
    transport=direct
    role=host            (the friend: role=guest)
    peer_host=<your FRIEND's address>
    peer_port=37015      (the same on both)


AFTER A GAME UPDATE
-------------------
Co-op+ is made for one exact game version. When Steam updates the game, the panel
(F8) says "Co-op+ is off: this mod was made for game version ..." and the game simply
runs without it. Nothing breaks; wait for a Co-op+ update, and both players install it.

PROBLEMS?
---------
The mod writes cdmp.log in the game's bin64 folder. Send that file (and say what
happened) when you report a problem; BUG-REPORT.txt is a short form to fill in. It contains in-game data such as positions,
not personal information.


LICENSE
-------
GPL v3 (LICENSE.txt). The full source code of exactly this build is in source.zip.
Third-party parts and their licenses: THIRD-PARTY-NOTICES.txt.
