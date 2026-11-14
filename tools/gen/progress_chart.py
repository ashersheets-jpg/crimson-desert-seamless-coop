# The progress-by-task chart (regenerate when a release is ready; edit the rows, run from the repo root).
import matplotlib; matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch
G = [
("FOUNDATION", [
 ("Shared world (join from title, co-op saves)", 90, "Moved to game 1.0.0.3663 (Steam update of 13 Nov). Testers joined and played on the build before."),
 ("Same NPCs and townsfolk for both players", 85, "Testers still saw desync, but with two different builds in one session. To be judged again."),
 ("Guest talks to NPCs on its own progress", 65, "Greetings and shops work. Longer conversations not checked yet."),
 ("Merchants for the guest (buy / sell)", 90, "Works in tests. A tester's blacksmith loop was most likely the town lag; watching."),
 ("World events and sieges", 35, "A camp's reinforcement event ran with both players in the fight (one PC). Sieges not tried."),
 ("Shared map", 60, "Map fog sharing built, needs a live test. Shared knowledge not started."),
 ("Other mounts and vehicles", 40, "The joined player calls, mounts and rides its horse in our test (one PC). Wyverns and wagons not tried."),
 ("Mounted combat damage", 70, "A mounted guest hits, kills and is hit in the saddle (one PC). The host mounted not tried."),
 ("Boss mechanics for two", 15, "Testers: a story boss was seen by the host only. It comes with shared cutscenes (next row)."),
 ("Shared cutscenes", 55, "Ready check built (experimental setting): the story waits for both, works in game on one PC. Friend's own cutscene camera next."),
 ("Getting to your friend (travel, call them to you)", 75, "Was slow hops (3 min for 3 km). Beyond 250 m it is the game's own travel now. Out, not confirmed."),
 ("Quests (your own progress, optional shared credit)", 72, "A new co-op save now starts both players at the first quest (out now). Shared credit is off."),
]),
("ITEMS", [
 ("Item gifting and trading", 78, "Moved to the new game version (its inventory tables changed); not re-tested there yet. No trade window."),
 ("Shared stash", 50, "In the posted build, on the host's warehouse. Not tested yet with an unlocked warehouse."),
 ("Loot for each player (Lootr)", 85, "Plants, world gear, chests and enemy loot are posted. Chests and bodies are checked for two."),
]),
("MINIGAMES AND CHALLENGES", [
 ("Real PvP minigames", 10, "Not started beyond what already exists."),
 ("Arm-wrestling table", 50, "Needs a hands-on check."),
 ("Duel extras and scoreboard", 5, "Not started."),
 ("Emotes and group photo mode", 5, "First look only: the game has its own social actions. Nothing built."),
 ("Player interaction menu (hold interact on a player)", 90, "Works on keyboard and controller. Give an item is in it."),
]),
("COMBAT AND SAFETY", [
 ("Fighting together (the guest's hits, no false crimes)", 88, "Works 484 m from the host. Stuck after an Axiom Force grab: not reproduced in three tests."),
 ("Teammate revive rework", 93, "Posted. Both players' revives pass the checklist on the new game version. No tester report yet."),
 ("Combat feedback for both", 30, "Should come with the shared world. Needs a hands-on test."),
 ("Anti-grief protections", 80, "Only people the host has talked to are protected now. In the posted build. Needs a hands-on check."),
 ("Partner audio", 30, "Should come with the shared world. Needs a hands-on test."),
]),
("LAST", [
 ("3-4 player co-op", 0, "Only after two-player play is stable."),
 ("Release (package, installer, save protection)", 82, "A 23-step checklist runs before every post. Build 5a81a327 for game 1.0.0.3663 passed all 23 steps and is out."),
]),
]
BG="#1e1f24"; TRACK="#2e3038"; BAR="#5b9bd5"; INK="#f2f2f2"; MUT="#a6a8ad"
rows = sum(len(g[1]) for g in G); ROW=0.554; GH=0.554
H = 1.9 + rows*ROW + len(G)*GH + 0.9
fig = plt.figure(figsize=(21.5, H), dpi=100, facecolor=BG)
ax = fig.add_axes([0,0,1,1]); ax.set_xlim(0,21.5); ax.set_ylim(H,0); ax.axis('off')
ax.text(0.26,0.52,"Co-op+ big update: progress by task",color=INK,fontsize=26,fontweight='bold',va='center')
ax.text(0.26,1.08,"13 Nov 2026.  Percentages are the developer's estimates, not measurements.  Build 5a81a327 for game 1.0.0.3663 is out (tested on one PC, two copies of the game). First two-PC reports are in.",color=MUT,fontsize=14,va='center')
y=1.9
for name, items in G:
    ax.text(0.26,y,name,color=MUT,fontsize=12.5,fontweight='bold',va='center'); y+=GH*0.95
    for label,pct,note in items:
        ax.text(0.39,y,label,color=INK,fontsize=15,va='center')
        ax.add_patch(FancyBboxPatch((6.5,y-0.1),3.9,0.2,boxstyle="round,pad=0,rounding_size=0.04",fc=TRACK,ec='none'))
        if pct>0: ax.add_patch(FancyBboxPatch((6.5,y-0.1),3.9*pct/100,0.2,boxstyle="round,pad=0,rounding_size=0.04",fc=BAR,ec='none'))
        ax.text(10.56,y,f"{pct}%",color=INK,fontsize=15,fontweight='bold',va='center')
        ax.text(11.31,y,note,color=MUT,fontsize=13.5,va='center')
        y+=ROW
    y+=GH*0.05
ax.text(0.26,y+0.1,"Dropped by request: joining mid-fight or mid-game (joining is from the title screen).",color=MUT,fontsize=12,va='center')
fig.savefig("docs/drafts/progress-2026-11-13-b.png",facecolor=BG)
