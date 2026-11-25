# The progress-by-task chart (regenerate when a release is ready or the owner asks; edit the rows, run from the repo
# root). Each row: (task, percent now, percent on the owner's previous chart, note). The bar's lighter part and the
# "+N" are what was gained since that chart.
import matplotlib; matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch

SINCE = "13 Nov"
OUT = "docs/drafts/progress-2026-11-24.png"
TITLE_DATE = "24 Nov 2026, 20:30"
G = [
("FOUNDATION", [
 ("Shared world (join from title, co-op saves)", 95, 90, "Testers play it over Steam on two PCs. Two game updates ported. A 45-minute session: no leak, no slowdown."),
 ("Same NPCs and townsfolk for both players", 88, 85, "Invisible NPCs for the friend fixed (seen on two PCs); the town desync fixed in 527e283a. Bosses unchecked."),
 ("Guest talks to NPCs on its own progress", 75, 65, "Greetings and talks fixed (testers saw them greyed out); the NPC now stops for the friend. Story talks unchecked."),
 ("Merchants for the guest (buy / sell)", 90, 90, "The friend's shop, alone and beside the host, passes every checklist. Buying last re-checked before game 3732."),
 ("World events and sieges", 40, 35, "A camp's reinforcement event runs with both players fighting in it (about 25 checklist runs). Sieges not tried."),
 ("Shared map", 70, 60, "Map fog sharing seen working (17 Nov). Shared knowledge is built; no real learn seen yet."),
 ("Other mounts and vehicles", 55, 35, "The wyvern works both ways; horse fixes are out. Taming: likely cause found, fix written. Wagons not tried."),
 ("Mounted combat damage", 100, 70, "Done: the friend's blows from the saddle hurt and kill in every checklist."),
 ("Boss mechanics for two", 25, 20, "A tester's boss was invisible to the friend: likely fixed (seen for other NPCs), not seen at a boss yet."),
 ("Shared cutscenes", 70, 30, "Both games play a story scene together after a ready check (one PC). Off by default: story NPCs show twice."),
 ("Getting to your friend (travel, call them to you)", 85, 75, "The hop now lands on the friend's own path (flat ground, a slope, a camp fight). Deep water not tried."),
 ("Quests (your own progress, optional shared credit)", 78, 72, "Own progress works. Shared credit works (one PC) but is off: your call. A tester got no credit in story mode."),
]),
("ITEMS", [
 ("Item gifting and trading", 90, 80, "Give and trade (each offers a stack, both accept) work and are on. No two-PC report yet."),
 ("Shared stash", 50, 50, "In the builds, on the host's warehouse. Not tested yet with an unlocked warehouse."),
 ("Loot for each player (Lootr)", 88, 85, "Plants, gear, chests and bodies are per player in every checklist. Loot memory across sessions written."),
]),
("MINIGAMES AND CHALLENGES", [
 ("Real PvP minigames", 10, 10, "Not started: waits on your call (where the two players sit for arm wrestling)."),
 ("Arm-wrestling table", 95, 50, "Each player plays their own match: fee, grip, win or loss, prize. Edward's solo-play bug is being looked at."),
 ("Duel extras and scoreboard", 75, 5, "Wagers, the duel ground (leaving it loses the round), rematch, scoreboard, titles. Spectators need 3 players."),
 ("Emotes and group photo mode", 50, 5, "The game's photo mode shows both players; our name tags hide in it. Emotes: none in the game (your call)."),
 ("Player interaction menu (hold interact on a player)", 92, 90, "Works on keyboard and controller on the new game version. Give and trade are in it."),
]),
("COMBAT AND SAFETY", [
 ("Fighting together (the guest's hits, no false crimes)", 92, 88, "Enemies now fight the friend too, not only the host. Works far from the host. The Axiom grab: not reproduced."),
 ("Teammate revive rework", 95, 93, "Both players' revives pass every checklist. An optional death penalty was added (off by default)."),
 ("Combat feedback for both", 55, 30, "Moving enemies on the friend's screen are now 0.3 s behind (was 1.1 s): fewer hits that look like misses."),
 ("Anti-grief protections", 82, 80, "Only people the host has talked to are protected, in every build. Needs a hands-on check."),
 ("Partner audio", 60, 30, "The friend's swings and footsteps play on your game and are heard (measured). Voices likely; testers asked."),
]),
("LAST", [
 ("3-4 player co-op", 0, 0, "Only after two-player play is stable."),
 ("Release (package, installer, save protection)", 88, 80, "21 builds posted since 13 Nov, each after the full checklist (now 28 steps). Ships when every task is flawless."),
]),
]
SUB1 = (f"{TITLE_DATE}.  Percentages are the developer's estimates, not measurements.  Latest playtest build: "
        "0.1.0-alpha+527e283a for game 1.0.0.3732.")
SUB2 = ("Since 13 Nov: 21 builds posted, two game updates absorbed (1.0.0.3663, 1.0.0.3732), the testers' first "
        "two-PC sessions over Steam, the checklist grown from 23 to 28 steps.")
FOOT1 = ("Open from testers: fishing and mining for the friend, the horse taming (fix written, in test), a solo "
         "arm-wrestling bug, a boss fight check.")
FOOT2 = "Dropped by request: joining mid-fight or mid-game (joining is from the title screen)."

BG = "#1e1f24"; TRACK = "#2e3038"; BAR = "#5b9bd5"; GAIN = "#a9d1f7"; INK = "#f2f2f2"; MUT = "#a6a8ad"
W = 23.4
X_LABEL, X_BAR, BAR_W, X_PCT, X_GAIN, X_NOTE = 0.39, 6.5, 3.9, 10.56, 11.36, 12.05
rows = sum(len(g[1]) for g in G); ROW = 0.554; GH = 0.554
H = 2.75 + rows * ROW + len(G) * GH + 1.25
fig = plt.figure(figsize=(W, H), dpi=100, facecolor=BG)
ax = fig.add_axes([0, 0, 1, 1]); ax.set_xlim(0, W); ax.set_ylim(H, 0); ax.axis('off')


def bar(x0, w, color):
    ax.add_patch(FancyBboxPatch((x0, y - 0.1), w, 0.2, boxstyle="round,pad=0,rounding_size=0.04", fc=color, ec='none'))


ax.text(0.26, 0.52, "Co-op+ big update: progress by task", color=INK, fontsize=26, fontweight='bold', va='center')
ax.text(0.26, 1.08, SUB1, color=MUT, fontsize=14, va='center')
ax.text(0.26, 1.5, SUB2, color=MUT, fontsize=14, va='center')
# Legend: the two parts of a bar.
y = 1.98
bar(0.30, 0.42, BAR)
ax.text(0.82, y, f"done by {SINCE}", color=MUT, fontsize=13, va='center')
bar(2.55, 0.42, GAIN)
ax.text(3.07, y, f"gained since {SINCE} (also the +N after each percentage)", color=MUT, fontsize=13, va='center')
y = 2.75
for name, items in G:
    ax.text(0.26, y, name, color=MUT, fontsize=12.5, fontweight='bold', va='center'); y += GH * 0.95
    for label, pct, was, note in items:
        ax.text(X_LABEL, y, label, color=INK, fontsize=15, va='center')
        bar(X_BAR, BAR_W, TRACK)
        old = min(was, pct)
        if old > 0:
            bar(X_BAR, BAR_W * old / 100, BAR)
        if pct > old:
            gap = 0.03 if old > 0 else 0.0  # a thin surface gap between the two parts
            bar(X_BAR + BAR_W * old / 100 + gap, BAR_W * (pct - old) / 100 - gap, GAIN)
        ax.text(X_PCT, y, f"{pct}%", color=INK, fontsize=15, fontweight='bold', va='center')
        if pct > was:
            ax.text(X_GAIN, y, f"+{pct - was}", color=MUT, fontsize=13, va='center')
        ax.text(X_NOTE, y, note, color=MUT, fontsize=13.5, va='center')
        y += ROW
    y += GH * 0.05
ax.text(0.26, y + 0.1, FOOT1, color=MUT, fontsize=12, va='center')
ax.text(0.26, y + 0.5, FOOT2, color=MUT, fontsize=12, va='center')
fig.savefig(OUT, facecolor=BG)
print("wrote", OUT, f"{W:.1f} x {H:.1f} in")
