CRIMSON DESERT SEAMLESS CO-OP+  -  SHARED WORLD  (alpha, @VERSION@)
===================================================================

Play Crimson Desert with a Steam friend in ONE shared world: your friend joins your
actual world, with their own character. Unofficial fan mod; not made or endorsed by
Pearl Abyss. ALPHA: expect rough edges. Your own saves are never written in co-op
(see SAVES below), and the installer backs them up anyway.

Built on "Crimson Desert Multiplayer" by ByJunter (its NativeServer shared world) and
its port to the current game version by nouzert1. See CREDITS.md.


BEFORE YOU START
----------------
- Both players: Crimson Desert on Steam, game version 1.0.0.3732 (the current
  patch when this was built). On another version the mod switches itself off and
  the game runs normally.
- Both players need THIS SAME ZIP. Different builds are refused at the title menu
  ("... has another Co-op+ build").
- You must be Steam friends, with the Steam overlay on (Steam > Settings > In Game >
  Enable the Steam Overlay). No Steam invite possible? See DIRECT PLAY below.


INSTALL (each player, game closed)
---------------------------------
1. Unzip this folder anywhere (Desktop is fine).
2. Double-click  "Install Co-op+.cmd".
   It finds the game through Steam, copies your saves to
   Documents\CrimsonDesert-CoopPlus-SaveBackups, and puts these files into the game's
   bin64 folder: winmm.dll (Ultimate ASI Loader), cdmp.asi (the shared-world base),
   coopplus.asi (Co-op+), coopplus.ini (your Co-op+ settings), cdmp.ini (Co-op+'s own
   launch file: it rewrites it at each start, do not edit it) and
   cdcoop\config\scaling.toml.
3. Start the game from Steam as usual. The Co-op menu stands in for the title screen.

What the installer changes: only the game's bin64 folder (the files listed in
bin64\coopplus-installed.txt). Anything it would replace is kept as *.before-coopplus.
An update keeps your coopplus.ini (the new defaults go next to it as coopplus.ini.new).

To remove it: double-click "Uninstall Co-op+.cmd" (game closed). It removes exactly
the files it installed and puts back any *.before-coopplus file.
To play without it for a while: "Vanilla mode on-off.cmd" switches Co-op+ off and on
again, keeping your settings.
Something wrong? "Diagnose Co-op+.cmd" writes a report and a .zip (report + logs) to
your Desktop and puts the .zip on the clipboard: paste it (Ctrl+V) into a message.
Steam names and ids, your Windows user and PC name and IP addresses are taken out.


MANUAL INSTALL / VORTEX / MOD ORGANIZER (instead of the installer)
------------------------------------------------------------------
Use the "...-mod-manager.zip" download. It has one folder, bin64, holding exactly the
files the installer would copy, plus the Co-op+ readme / licence files.
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


QUICK START (the first time, a few minutes)
-------------------------------------------
1. Both players: install (above), then start the game from Steam. The Co-op menu
   stands in for the game's title screen. (W / S or the D-pad move, Space / A picks,
   Esc / B goes back.)
2. Host: "Invite Friends". The Steam overlay opens: pick your friend.
3. Friend: accept the invite in Steam (the pop-up, or right-click the host in the
   friends list > "Join Game"). Stay at the Co-op menu: it says "You are in <host>'s
   party". Both menus list the party top right; the crown marks the host.
4. Host: "Start Co-op", then how the journey begins:
   - Continue Our Co-op Save: where the two of you left off (greyed until you have
     played together once).
   - Start From My Solo Progress: your friend joins your story where you are (a co-op
     save of your progress is made; your own save is not touched).
   - New Co-op Save: both of you begin the story; the host picks the difficulty.
5. Both games restart once into co-op (the host's first, then the friend's; about
   half a minute). The friend sees "Waiting for <host>" and needs to do nothing: their
   game follows the host's and joins as soon as the host is in the world. A friend who
   lands far away is moved beside the host.
6. The next time you start the game it is your own solo game again; co-op starts only
   from this menu. "Start Solo" plays alone.


ONE SHARED WORLD: WHAT IT MEANS
-------------------------------
The host's game runs the world; the friend plays in it with their own character.
- Townsfolk and merchants are the same people for both of you. The friend talks to
  them, buys and sells with their own money and stock, also far from the host.
- Enemies are shared: both of you hit and kill the same ones, and an enemy fights the
  player who fights it. With a friend in, enemies have about 1.5x health (Co-op tab >
  Combat & Loot). No friendly fire.
- Items in the world: each of you can take each item once ("Items in the world: each
  their own", Co-op tab > World): plants, ore, chests and the loot of bodies. A chest
  your friend opened keeps your share for you at the chest (pinned on your map).
- Quests and story: each of you keeps and plays your OWN story in the shared world;
  you can do your own steps and fight together. Optional (Co-op tab > Saves & Story):
  "Shared quest credit" (a step you are BOTH on is done for both when one of you does
  it) and "Story scenes together (experimental)" (a story scene
  waits until both of you are there and both press, then each game plays it).
- Time of day and weather follow the host.
- Mounts: each of you sees the other ride (your own horse, a borrowed or wild one,
  Blackstar), and an animal one of you carries stays in their hands for the other.
- Crimes are your own: hitting townsfolk or stealing makes YOU wanted, not your
  friend. People refuse to talk to a wanted player (the game's own rule).
- The game lets only ONE player at a time talk to a person: while your friend talks
  to someone, that person is busy for you (a message says so).
- Two players at a time.


PLAYING TOGETHER
----------------
Co-op+ adds no hotkeys in the shared world: everything is on your friend or in the
Co-op tab.
- Hold the game's Interact (R; pad A) on your friend (within 3 m, in the middle of the
  screen): a menu with the challenges (below), a note on giving and trading items,
  and Leave. W / S (D-pad) choose, Space (A) picks, Esc (B) closes.
- The Co-op tab: open the game's pause menu (Esc / Menu); "Co-op" is the last tab,
  after Others (step to it with E / RB, or back from Map with Q / LB). In it:
  Session (who you play with, the connection, Go to my friend, Bring my friend to me,
  quick messages, Resynchronise, Leave the session), Shared stash, the settings
  (below), Controls and Status. Settings are saved at once.
- A challenge or a call from your friend: a prompt appears; hold Interact to accept
  ("Accept: a duel", "Go to <name>", "Follow <name>"). Ignore it to let it lapse, or
  decline in the Co-op tab.
- Your friend's name shows over their head (Co-op tab > Display), and their health on
  the left of your screen.
- Chat (keyboard): Shift+F2, type, Enter sends, Esc cancels. Quick messages (Go here,
  Danger here!, Loot here, Follow me, Wait for me, Help!, Thanks!, Ready?, Hold still:
  photo!) are in the Co-op tab's Session; the three "here" ones put a pin on your
  friend's map.
- Down in co-op: the game's death screen ("Death has found you."): Q / X instant
  revive (30% health), U / Y revive at the checkpoint. For 30 seconds your friend can
  also get you up where you fell, with full health: they stand by you and hold
  Interact for 3 seconds. At a boss you can wait for your friend instead ("Your
  partner is still fighting the boss"; Co-op tab > Reviving). If the host turned on the
  silver penalty (Co-op tab > Reviving), getting up by yourself costs that share of
  your own silver; the death screen says how much.
- Group photos: the game's photo mode (pause menu) shows your friend too; Co-op+'s own
  labels hide while it is open. The quick message "Hold still: photo!" asks them to pose.
- Menus: opening the map, inventory or pause menu does not pause the shared world.


CHALLENGES (hold Interact on your friend)
-----------------------------------------
Pick one; your friend gets the invite and accepts by holding Interact (it lapses
after a minute). Each ends with a Victory or Defeat card; most are first to 2 rounds.
- Duel: a ring marks the duel ground (its size: Co-op tab > Challenges); bring your
  friend down to 3% health to win the round (nobody dies); 10 s outside the ring
  loses the round.
- Rock, paper, scissors: U (scissors), F (paper), E (rock); pad X, Y, B.
- Hunting contest: 3-minute rounds; small game 1 point, deer and foxes 2, bears,
  wolves and boars 3.
- Race to the waypoint: the host's map destination (right-click on the map) is the finish;
  the first within 12 m wins the round.
- Hide and seek: 45 s to hide within 100 m while the seeker's screen is dark, then 3
  minutes to find the hider (within 4 m); "Cold / Warm / Hot" says how close you are.
- Fishing contest: six minutes; every fish you keep scores (river 1, sea 2, legendary 5).
- Arm / hand wrestling against each other: not yet (see KNOWN ISSUES). Each of you can play
  the arm-wrestling tables against the townsfolk (checked at the Hernand inn): talk to the
  organizer; the fee comes from your own purse and the prize goes to it.


GIVING, TRADING, WAGERS, THE SHARED STASH
-----------------------------------------
In your inventory, Discard on an item while your friend is within 10 m asks:
- Give to <name>: the item goes to their bag.
- Offer in a trade: each of you offers one stack, both accept, each game gives its
  offer (it lapses after 2 minutes).
- Stake on the challenge (while a challenge is sent, received or on): when the match
  ends the loser's stake goes to the winner; if only one of you staked, nothing
  changes hands.
- Put in the shared stash (from anywhere, once the host has a warehouse): the stash
  IS the host's warehouse. Take items out in the Co-op tab > Shared stash.
- Discard (the game's own) or Cancel.


THE CO-OP TAB'S SETTINGS
------------------------
- Session: Who can join (any Steam friend, or only friends you invite; from the next
  start).
- Saves & Story: Shared quest credit; Story scenes together (experimental); My
  friend's story: shown, or hidden (no spoilers: their quest steps you have not
  reached are not named). Shared quest credit and Story scenes together are the
  host's choice and apply at the next Start Co-op (Items in the world too).
- Combat & Loot: Tougher enemies with a guest; Enemy health with a guest; Boss health
  with a guest (the host's values count).
- Reviving: Revive each other; After a checkpoint revive: stay at the checkpoint, or
  back to my friend; Wait for my friend if I die at a boss; Silver lost when you get up
  by yourself after a death (none, 10 %, 25 %, half, all; off by default; the host's
  choice applies to both of you from the next Start Co-op; your friend getting you up
  costs nothing).
- World: Items in the world (each their own, or first come); Weather follows the host.
- Display: Ping and packet loss on screen; Player colours (gold host / blue guest, or
  colour-blind safe); My friend's map pin colour; Name over my friend.
- Challenges: the duel ground's size (12 / 20 / 35 m).
- Status: a small status panel top left.


SAVES: WHAT STAYS AFTER A SESSION
---------------------------------
- Co-op never writes your own saves. Each friend you play with has a co-op save of its
  own (%LOCALAPPDATA%\Pearl Abyss\CD\coop_worlds\with_<friend>); "Continue Our Co-op
  Save" carries on from there next time.
- The friend keeps their own character and progress between sessions.
- Without co-op the game starts from your own saves, as always.


KNOWN ISSUES (shared world playtest)
------------------------------------
- Joining only from the title screen (not during play). Two players only.
- Tested mostly with two copies of the game on one PC; two PCs teach us something new
  in every session: please report what you see, with the report zip of BOTH players
  (Diagnose Co-op+.cmd).
- Mining with the hand drill and fishing as the joined player have problems we have
  not fixed yet.
- The host's armour dye does not show on the joined player's screen yet.
- A tree the joined player cuts down can show twice for the host.
- Taming a wild horse as the joined player can leave them stuck at the end: if it
  happens, please send both zips.
- City NPCs sitting on benches may stand in a T-pose on the joined player's screen
  (reported once, not seen in our tests).
- Boss fights are not tested with two players yet.
- At the very start of a New Co-op Save the friend's time of day can differ from the
  host's (cosmetic: different light).
- Story scenes that start from a TALK do not wait for both of you yet (only the first
  scene of a story step does).
- Arm wrestling and the game's other minigames against each other: not yet.
- After a game update Co-op+ switches itself off until it is updated (the game runs
  normally, solo).


SHORTCUTS (optional)
--------------------
Co-op+ binds nothing but Chat (Shift+F2) in the shared world. To add your own, edit
bin64\coopplus.ini (game closed) and add a [keys] section, one line per action:
    [keys]
    Panel=F8              (opens / closes the Co-op tab)
    GoToFriend=Ctrl+F4
    Ping_pad=Left         (a pad binding means: hold View / touchpad + that button)
Actions: Invite Leave Ping GoToFriend Challenge Decline Panel Nameplates Resync
ReviveFriend Summon Chat Help. Keys: A-Z, 0-9, F1-F12, Esc Tab Space Enter Insert
Delete Home End PageUp PageDown arrows Backspace, with Ctrl+ and/or Shift+. Pad (_pad):
A B X Y LB RB L3 R3 Up Down Left Right. Interact and the menu keys cannot be changed.
A problem with a line shows once as a notice when you are first in the world.


IS IT SAFE? (ANTI-CHEAT, ACCOUNTS, CROSS-SAVE)
---------------------------------------------
- Crimson Desert has no anti-cheat. Its Denuvo is copy protection only, and the game
  has no online play of its own. Co-op+ changes only your local game.
- Should an update ever add an anti-cheat, Co-op+ notices at start, installs nothing
  at all and tells you in a small window; the game runs normally.
- Co-op+ never touches your account, the store, cross-save or Steam Cloud, and sends
  nothing anywhere except to the friend you play with. It never changes your own saves
  (co-op plays in its own co-op save).


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
you sent it to can join. The mod listens only on the VPN / LAN adapter (UDP 37015).
"Back to Steam Invite" returns to Steam.


AFTER A GAME UPDATE
-------------------
Co-op+ is made for one exact game version. When Steam updates the game, Co-op+
switches itself off and the game simply runs without it (solo). Nothing breaks; wait
for a Co-op+ update, and both players install it.


PROBLEMS?
---------
Run "Diagnose Co-op+.cmd" (game closed) and send the .zip it puts on your Desktop, with
what happened and roughly when; BUG-REPORT.txt is a short form to fill in. From BOTH
players if you can: each game knows only its own side. The logs are cdmp.log and
coopplus.log in the game's bin64 folder; they hold in-game data such as positions, not
personal information.


LICENSE
-------
GPL v3 (LICENSE.txt). The full source code of exactly this build is in source.zip
(Co-op+) and source-native.zip (the shared-world base, commit @NATIVE@).
Third-party parts and their licenses: THIRD-PARTY-NOTICES.txt.
