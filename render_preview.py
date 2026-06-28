"""
Renders a pixel-perfect preview of the attention dashboard as a PNG image
using only matplotlib — no Tkinter or webcam needed.
"""

import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
from matplotlib.patches import FancyBboxPatch, Arc, FancyArrowPatch
from matplotlib.gridspec import GridSpec
import matplotlib.patheffects as pe
import math

# ── Colour palette ─────────────────────────────────────────────────────────────
BG_DARK   = "#0d0d1a"
BG_CARD   = "#1a1a2e"
BG_CARD2  = "#16213e"
ACCENT    = "#2ecc71"
TEXT_MAIN = "#e0e0f0"
TEXT_DIM  = "#8888aa"
RED       = "#e74c3c"
ORANGE    = "#f39c12"
GREEN     = "#2ecc71"

# ── Synthetic session data ─────────────────────────────────────────────────────
np.random.seed(7)
t = np.linspace(0, 420, 300)
base = 78 + 12 * np.sin(t / 55)
dip  = np.exp(-((t - 190)**2) / (2 * 28**2)) * 45
noise = np.random.normal(0, 5, 300)
scores = np.clip(base - dip + noise, 0, 100)
current_score = 74.0

# ── Figure setup ───────────────────────────────────────────────────────────────
fig = plt.figure(figsize=(14, 8.5), facecolor=BG_DARK, dpi=120)

# ── Title bar ──────────────────────────────────────────────────────────────────
title_ax = fig.add_axes([0, 0.935, 1, 0.065], facecolor=BG_CARD2)
title_ax.set_xlim(0, 1); title_ax.set_ylim(0, 1)
title_ax.axis("off")
title_ax.text(0.02, 0.5, "◉  AI Student Attention Detection System",
              color=ACCENT, fontsize=14, fontweight="bold", va="center")
title_ax.text(0.72, 0.5, "● RUNNING",
              color=GREEN, fontsize=10, fontweight="bold", va="center")
title_ax.text(0.87, 0.5, "14:32:18",
              color=TEXT_DIM, fontsize=11, va="center")
title_ax.axhline(0, color="#2a2a44", lw=1)

# ── Camera feed panel ──────────────────────────────────────────────────────────
cam_ax = fig.add_axes([0.01, 0.08, 0.42, 0.84], facecolor=BG_CARD)
cam_ax.set_xlim(0, 640); cam_ax.set_ylim(0, 480)
cam_ax.axis("off")
cam_ax.set_title("LIVE FEED", color=TEXT_DIM, fontsize=8, pad=4)

# Simulated face background
face_bg = plt.Circle((320, 240), 155, color="#111122", zorder=1)
cam_ax.add_patch(face_bg)

# Draw a simple face silhouette
face = plt.Circle((320, 252), 130, color="#2a2a3e", zorder=2)
cam_ax.add_patch(face)

# MediaPipe-style green landmark dots
rng = np.random.default_rng(42)
angles = np.linspace(0, 2*np.pi, 68)
r_face = 115
xs = 320 + r_face * np.cos(angles) + rng.normal(0, 4, 68)
ys = 252 + r_face * 0.85 * np.sin(angles) + rng.normal(0, 4, 68)
cam_ax.scatter(xs, ys, s=2, color=ACCENT, alpha=0.7, zorder=5)

# Eyes
for ex, ey in [(270, 275), (375, 275)]:
    eye_ring = plt.Circle((ex, ey), 22, color="#0d0d1a", zorder=3)
    eye_iris = plt.Circle((ex, ey), 14, color="#1a3a6e", zorder=4)
    eye_pupil= plt.Circle((ex, ey), 7,  color="#050510", zorder=5)
    for p in [eye_ring, eye_iris, eye_pupil]: cam_ax.add_patch(p)
    # landmark dots around eye
    for a in np.linspace(0, 2*np.pi, 8):
        cam_ax.plot(ex+20*np.cos(a), ey+12*np.sin(a), '.', color=ACCENT,
                    ms=2, zorder=6)

# Nose
cam_ax.plot([320, 310, 330], [252, 222, 222], color=ACCENT,
            lw=1, alpha=0.6, zorder=5)

# Mouth (landmarks)
mouth_x = np.linspace(285, 360, 12)
mouth_y = 210 + 8 * np.sin(np.linspace(0, np.pi, 12))
cam_ax.plot(mouth_x, mouth_y, '.', color=ACCENT, ms=2.5, zorder=6)
cam_ax.plot(mouth_x, mouth_y, color=ACCENT, lw=0.8, alpha=0.4, zorder=5)

# Eyebrows
for bx, by in [(265, 300), (370, 300)]:
    brow_x = np.linspace(bx-22, bx+22, 6)
    brow_y = by + 4 * np.sin(np.linspace(0, np.pi, 6))
    cam_ax.plot(brow_x, brow_y, '.', color=ACCENT, ms=2.5, zorder=6)

# Status overlay
cam_ax.add_patch(FancyBboxPatch((8, 8), 175, 28, boxstyle="round,pad=2",
                                 facecolor=BG_CARD2, alpha=0.85, zorder=7))
cam_ax.text(16, 26, f"Score: {current_score:.0f}%", color=GREEN,
            fontsize=9, fontweight="bold", zorder=8)
cam_ax.text(16, 14, "Face: DETECTED", color=TEXT_DIM, fontsize=7.5, zorder=8)

cam_ax.add_patch(FancyBboxPatch((8, 440), 130, 22, boxstyle="round,pad=2",
                                 facecolor=BG_CARD2, alpha=0.85, zorder=7))
cam_ax.text(16, 454, "MediaPipe Landmarks", color=ACCENT, fontsize=7, zorder=8)

for spine in cam_ax.spines.values():
    spine.set_edgecolor(ACCENT)
    spine.set_linewidth(1)
cam_ax.set_xticks([]); cam_ax.set_yticks([])

# ── Attention Ring ─────────────────────────────────────────────────────────────
ring_ax = fig.add_axes([0.45, 0.61, 0.21, 0.30], facecolor=BG_CARD)
ring_ax.set_xlim(-1.3, 1.3); ring_ax.set_ylim(-1.3, 1.3)
ring_ax.axis("off")
ring_ax.set_title("ATTENTION", color=TEXT_DIM, fontsize=7.5, pad=2)

def draw_ring(ax, score, cx=0, cy=0, r=1.0, lw=16):
    col = GREEN if score >= 70 else (ORANGE if score >= 40 else RED)
    # Background ring
    theta = np.linspace(0, 2*np.pi, 360)
    ax.plot(cx + r*np.cos(theta), cy + r*np.sin(theta),
            color="#2a2a44", lw=lw, solid_capstyle="round", zorder=2)
    # Foreground arc
    arc_end = (score / 100.0) * 2 * np.pi
    theta2 = np.linspace(np.pi/2, np.pi/2 - arc_end, int(score*3)+1)
    ax.plot(cx + r*np.cos(theta2), cy + r*np.sin(theta2),
            color=col, lw=lw, solid_capstyle="round", zorder=3)
    ax.text(cx, cy+0.12, f"{score:.0f}", ha="center", va="center",
            color=col, fontsize=26, fontweight="bold", zorder=4)
    label = "HIGH" if score >= 70 else ("MEDIUM" if score >= 40 else "LOW")
    ax.text(cx, cy-0.28, label, ha="center", va="center",
            color=col, fontsize=10, fontweight="bold", zorder=4)

draw_ring(ring_ax, current_score)

# Add glow ring border
for spine in ring_ax.spines.values():
    spine.set_visible(False)

# ── Stats Grid ─────────────────────────────────────────────────────────────────
stats_ax = fig.add_axes([0.665, 0.61, 0.325, 0.30], facecolor=BG_CARD)
stats_ax.axis("off")
stats_ax.set_title("SESSION STATS", color=TEXT_DIM, fontsize=7.5, pad=2)

stats = [
    ("Blinks",        "34",     (100,180,255)),
    ("Yawns",         "3",      (255,160,80)),
    ("Distractions",  "7",      (231,76,60)),
    ("Session Time",  "07:07",  (180,180,200)),
    ("Face",          "YES",    (46,204,113)),
    ("Low Alert",     "--",     (180,180,200)),
]
cols = 2
rows_n = math.ceil(len(stats) / cols)
cell_w = 0.46; cell_h = 0.28; pad = 0.03
for i, (label, value, rgb) in enumerate(stats):
    row = i // cols
    col = i % cols
    x = col * (cell_w + pad) + 0.02
    y = 1 - (row+1) * (cell_h + pad) + 0.02
    col_hex = "#{:02x}{:02x}{:02x}".format(*rgb)
    stats_ax.add_patch(FancyBboxPatch((x, y), cell_w, cell_h,
                                       transform=stats_ax.transAxes,
                                       boxstyle="round,pad=0.01",
                                       facecolor=BG_CARD2, zorder=2,
                                       edgecolor="#2a2a3a", linewidth=0.8))
    stats_ax.text(x+0.03, y+cell_h-0.07, label, transform=stats_ax.transAxes,
                  color=TEXT_DIM, fontsize=7.5, zorder=3)
    stats_ax.text(x+0.03, y+0.04, value, transform=stats_ax.transAxes,
                  color=col_hex, fontsize=15, fontweight="bold", zorder=3)

# ── Live Graph ────────────────────────────────────────────────────────────────
graph_ax = fig.add_axes([0.45, 0.195, 0.545, 0.395], facecolor=BG_CARD2)
graph_ax.set_facecolor(BG_CARD2)
graph_ax.set_title("LIVE ATTENTION GRAPH", color=TEXT_DIM, fontsize=8, pad=4)

# Colour-segmented fill + line
for i in range(len(scores)-1):
    clr = GREEN if scores[i] >= 70 else (ORANGE if scores[i] >= 40 else RED)
    graph_ax.fill_between(t[i:i+2], scores[i:i+2], alpha=0.22, color=clr)
    graph_ax.plot(t[i:i+2], scores[i:i+2], color=clr, lw=1.3)

graph_ax.axhline(70, color=GREEN, linestyle="--", lw=0.8, alpha=0.5,
                 label="High (70)")
graph_ax.axhline(40, color=RED,   linestyle="--", lw=0.8, alpha=0.5,
                 label="Low (40)")

graph_ax.set_xlim(t[0], t[-1])
graph_ax.set_ylim(0, 105)
graph_ax.set_yticks([0, 40, 70, 100])
graph_ax.set_yticklabels(["0", "40", "70", "100"], color=TEXT_DIM, fontsize=7)
graph_ax.set_xlabel("Time (seconds)", color=TEXT_DIM, fontsize=8)
graph_ax.tick_params(colors=TEXT_DIM, labelsize=7)
for sp in graph_ax.spines.values():
    sp.set_edgecolor("#2a2a44")

# Legend
handles = [
    mpatches.Patch(color=GREEN,  label="High attention (≥70)"),
    mpatches.Patch(color=ORANGE, label="Medium (40-69)"),
    mpatches.Patch(color=RED,    label="Low (<40)"),
]
graph_ax.legend(handles=handles, loc="upper right", fontsize=6.5,
                facecolor=BG_CARD, edgecolor="#333", labelcolor=TEXT_MAIN)

# ── Bottom Buttons ────────────────────────────────────────────────────────────
btn_ax = fig.add_axes([0.0, 0.0, 1.0, 0.10], facecolor=BG_DARK)
btn_ax.axis("off")
btn_ax.set_xlim(0, 1); btn_ax.set_ylim(0, 1)

buttons = [
    (0.08, "  Start",          GREEN,    "#000"),
    (0.23, "  Stop",           RED,      "#fff"),
    (0.38, "  Generate Report","#3498db", "#fff"),
    (0.68, "  Quit",           "#555555", "#fff"),
]
for bx, label, bg, fg in buttons:
    btn_ax.add_patch(FancyBboxPatch((bx, 0.18), 0.14, 0.60,
                                    boxstyle="round,pad=0.02",
                                    facecolor=bg, zorder=2))
    btn_ax.text(bx+0.07, 0.50, label, ha="center", va="center",
                color=fg, fontsize=8.5, fontweight="bold", zorder=3)

# ── Camera panel border ───────────────────────────────────────────────────────
fig.patches.extend([
    FancyBboxPatch((0.01, 0.08), 0.42, 0.84,
                   boxstyle="round,pad=0.003",
                   transform=fig.transFigure,
                   facecolor="none",
                   edgecolor=ACCENT, linewidth=1.5)
])

# Save
out = "/home/user/Newcode/dashboard_preview.png"
fig.savefig(out, dpi=130, bbox_inches="tight", facecolor=BG_DARK)
print(f"Saved: {out}")
