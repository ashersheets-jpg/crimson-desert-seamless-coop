# The progress-by-task chart (regenerate when a release is ready; edit the rows, run from the repo root).
import matplotlib; matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch
G = [
("FOUNDATION", [
 ("Shared world (join from title, co-op saves)", 90, "Testers hit mixed builds, a host crash and a dead Start Co-op. All fixed in the next build."),
 ("Same NPCs and townsfolk for both players", 85, "Testers still saw desync, but with two different builds in one session. To be judged again."),
 ("Guest talks to NPCs on its own progress", 65, "Greetings and shops work. Longer conversations not checked yet."),
 ("Merchants for the guest (buy / sell)", 90, "Works in tests. A tester's blacksmith loop was most likely the town lag; watching."),
 ("World events and sieges", 35, "A camp's reinforcement event ran with both players in the fight (one PC). Sieges not tried."),
 ("Shared map", 60, "Map fog sharing built, needs a live test. Shared knowledge not started."),
 ("Other mounts and vehicles", 35, "Horses work in our tests. A tester's friend could call neither horse nor wyvern: not explained."),
 ("Mounted combat damage", 70, "A mounted guest hits, kills and is hit in the saddle (one PC). The host mounted not tried."),
 ("Boss mechanics for two", 20, "Not tested yet."),
 ("Shared cutscenes", 30, "The friend does not see the host's cutscenes; the host seems to jump about. Testers noticed."),
 ("Getting to your friend (travel, call them to you)", 75, "Was slow hops (3 min for 3 km). Beyond 250 m it is the game's own travel now. Next build."),
 ("Quests (your own progress, optional shared credit)", 72, "A new co-op save now starts both players at the first quest (next build). Shared credit is off."),
]),
("ITEMS", [
 ("Item gifting and trading", 80, "Giving from the inventory works both ways and is in the posted build. No two-way trade window."),
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
 ("Fighting together (the guest's hits, no false crimes)", 88, "Works 484 m from the host. A tester's friend got stuck after an Axiom Force grab: test is next."),
 ("Teammate revive rework", 93, "Posted. A tester's host crashed after going down and reviving: fixed in the next build."),
 ("Combat feedback for both", 30, "Should come with the shared world. Needs a hands-on test."),
 ("Anti-grief protections", 80, "Only people the host has talked to are protected now. In the posted build. Needs a hands-on check."),
 ("Partner audio", 30, "Should come with the shared world. Needs a hands-on test."),
]),
("LAST", [
 ("3-4 player co-op", 0, "Only after two-player play is stable."),
 ("Release (package, installer, save protection)", 80, "A 23-step checklist runs before every post. The next build is in it now: 18 of 23 steps passed so far."),
]),
]
BG="#1e1f24"; TRACK="#2e3038"; BAR="#5b9bd5"; INK="#f2f2f2"; MUT="#a6a8ad"
rows = sum(len(g[1]) for g in G); ROW=0.554; GH=0.554
H = 1.9 + rows*ROW + len(G)*GH + 0.9
fig = plt.figure(figsize=(21.5, H), dpi=100, facecolor=BG)
ax = fig.add_axes([0,0,1,1]); ax.set_xlim(0,21.5); ax.set_ylim(H,0); ax.axis('off')
ax.text(0.26,0.52,"Co-op+ big update: progress by task",color=INK,fontsize=26,fontweight='bold',va='center')
ax.text(0.26,1.08,"12 Nov 2026, late evening.  Percentages are the developer's estimates, not measurements.  Build 0.1.0-alpha+47135b83 is out; the next one is in its checklist, not posted (one PC, two copies of the game).",color=MUT,fontsize=14,va='center')
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
fig.savefig("docs/drafts/progress-2026-11-12-c.png",facecolor=BG)
