"""
Generates a PDF report summarising the attention session.
"""

import time
import os
import math
from datetime import datetime

try:
    from fpdf import FPDF
    _FPDF_AVAILABLE = True
except ImportError:
    _FPDF_AVAILABLE = False

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np


REPORT_DIR = "reports"


def _grade(avg_score: float) -> str:
    if avg_score >= 80:
        return "Excellent"
    if avg_score >= 60:
        return "Good"
    if avg_score >= 40:
        return "Fair"
    return "Needs Improvement"


def _save_graph(timestamps, scores, path: str):
    fig, ax = plt.subplots(figsize=(9, 3), facecolor="#1a1a2e")
    ax.set_facecolor("#16213e")

    # Colour segments
    ts = np.array(timestamps)
    sc = np.array(scores)
    for i in range(len(sc) - 1):
        clr = ("#2ecc71" if sc[i] >= 70 else
               "#f39c12" if sc[i] >= 40 else "#e74c3c")
        ax.fill_between(ts[i:i+2], sc[i:i+2], alpha=0.25, color=clr)
        ax.plot(ts[i:i+2], sc[i:i+2], color=clr, linewidth=1.5)

    ax.axhline(70, color="#2ecc71", linestyle="--", linewidth=0.8, alpha=0.5)
    ax.axhline(40, color="#e74c3c", linestyle="--", linewidth=0.8, alpha=0.5)
    ax.set_xlim(ts[0] if len(ts) else 0, ts[-1] if len(ts) else 1)
    ax.set_ylim(0, 105)
    ax.set_xlabel("Time (s)", color="white", fontsize=8)
    ax.set_ylabel("Attention Score", color="white", fontsize=8)
    ax.tick_params(colors="white", labelsize=7)
    for spine in ax.spines.values():
        spine.set_edgecolor("#444")
    ax.set_title("Attention Score Over Time", color="white", fontsize=10,
                 pad=6)
    plt.tight_layout()
    fig.savefig(path, dpi=130, facecolor=fig.get_facecolor())
    plt.close(fig)


def generate_report(stats: dict) -> str:
    """
    Build a PDF report from session stats dict.
    Returns the path to the saved PDF.
    """
    os.makedirs(REPORT_DIR, exist_ok=True)
    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    pdf_path   = os.path.join(REPORT_DIR, f"attention_report_{stamp}.pdf")
    graph_path = os.path.join(REPORT_DIR, f"_tmp_graph_{stamp}.png")

    avg   = stats.get("average_attention", 0.0)
    dur   = stats.get("duration_seconds",  0.0)
    blink = stats.get("blink_count",       0)
    yawn  = stats.get("yawn_count",        0)
    dist  = stats.get("distraction_events", 0)
    ts    = stats.get("timestamp_history", [])
    sc    = stats.get("score_history",     [])

    # Downsample for graph (max 600 pts)
    if len(sc) > 600:
        step = math.ceil(len(sc) / 600)
        ts = ts[::step]
        sc = sc[::step]

    _save_graph(ts, sc, graph_path)

    if not _FPDF_AVAILABLE:
        # Fallback: plain text report
        txt_path = pdf_path.replace(".pdf", ".txt")
        with open(txt_path, "w") as f:
            f.write(_text_report(stats, avg, dur, blink, yawn, dist, stamp))
        os.remove(graph_path)
        return txt_path

    pdf = FPDF()
    pdf.set_auto_page_break(auto=True, margin=15)
    pdf.add_page()

    # --- Header ---
    pdf.set_fill_color(22, 33, 62)
    pdf.rect(0, 0, 210, 40, "F")
    pdf.set_text_color(46, 204, 113)
    pdf.set_font("Helvetica", "B", 20)
    pdf.set_xy(10, 8)
    pdf.cell(0, 10, "AI Student Attention Report", ln=True)
    pdf.set_font("Helvetica", "", 10)
    pdf.set_text_color(180, 180, 200)
    pdf.set_x(10)
    pdf.cell(0, 6, f"Generated: {datetime.now().strftime('%d %B %Y  %H:%M:%S')}",
             ln=True)
    pdf.ln(8)

    # --- Score summary box ---
    pdf.set_fill_color(26, 26, 46)
    pdf.set_draw_color(46, 204, 113)
    pdf.set_line_width(0.5)
    pdf.rect(10, pdf.get_y(), 190, 50, "FD")
    y0 = pdf.get_y() + 5

    def _stat_cell(x, y, label, value, colour):
        pdf.set_xy(x, y)
        pdf.set_text_color(*colour)
        pdf.set_font("Helvetica", "B", 22)
        pdf.cell(50, 10, str(value), ln=False)
        pdf.set_xy(x, y + 10)
        pdf.set_text_color(150, 150, 170)
        pdf.set_font("Helvetica", "", 8)
        pdf.cell(50, 5, label)

    score_col = (46, 204, 113) if avg >= 70 else (243, 156, 18) if avg >= 40 else (231, 76, 60)
    _stat_cell(15,  y0, "AVG ATTENTION", f"{avg:.1f}%", score_col)
    _stat_cell(65,  y0, "BLINK COUNT",   str(blink),    (100, 180, 255))
    _stat_cell(115, y0, "YAWN COUNT",    str(yawn),     (255, 160, 80))
    _stat_cell(165, y0, "DISTRACTIONS",  str(dist),     (231, 76, 60))

    pdf.set_xy(10, y0 + 28)
    pdf.set_text_color(200, 200, 220)
    pdf.set_font("Helvetica", "B", 11)
    grade_col = score_col
    pdf.set_text_color(*grade_col)
    m, s = divmod(int(dur), 60)
    pdf.cell(0, 8,
             f"Overall Grade: {_grade(avg)}   |   Session Duration: {m}m {s}s",
             ln=True)

    pdf.ln(58 - (pdf.get_y() - y0) + 5)

    # --- Graph ---
    pdf.set_text_color(200, 200, 220)
    pdf.set_font("Helvetica", "B", 12)
    pdf.set_x(10)
    pdf.cell(0, 8, "Attention Timeline", ln=True)
    if os.path.exists(graph_path):
        pdf.image(graph_path, x=10, w=190)
    pdf.ln(4)

    # --- Observations ---
    pdf.set_text_color(200, 200, 220)
    pdf.set_font("Helvetica", "B", 12)
    pdf.cell(0, 8, "Key Observations", ln=True)
    pdf.set_font("Helvetica", "", 10)
    observations = _build_observations(avg, blink, yawn, dist, dur)
    for obs in observations:
        pdf.set_text_color(180, 180, 200)
        pdf.cell(5)
        pdf.multi_cell(180, 6, f"•  {obs}")

    # --- Recommendations ---
    pdf.ln(3)
    pdf.set_text_color(200, 200, 220)
    pdf.set_font("Helvetica", "B", 12)
    pdf.cell(0, 8, "Recommendations", ln=True)
    pdf.set_font("Helvetica", "", 10)
    recs = _build_recommendations(avg, yawn, dist)
    for rec in recs:
        pdf.set_text_color(100, 200, 150)
        pdf.cell(5)
        pdf.multi_cell(180, 6, f"✓  {rec}")

    # --- Footer ---
    pdf.set_y(-18)
    pdf.set_font("Helvetica", "I", 8)
    pdf.set_text_color(100, 100, 120)
    pdf.cell(0, 6, "AI Student Attention Detection System  |  Science Fair Project",
             align="C")

    pdf.output(pdf_path)
    if os.path.exists(graph_path):
        os.remove(graph_path)

    return pdf_path


def _build_observations(avg, blink, yawn, dist, dur):
    obs = []
    if avg >= 70:
        obs.append(f"Average attention was high at {avg:.1f}% — excellent focus throughout the session.")
    elif avg >= 40:
        obs.append(f"Average attention was moderate at {avg:.1f}% — some lapses were detected.")
    else:
        obs.append(f"Average attention was low at {avg:.1f}% — significant focus issues detected.")

    blink_rate = blink / max(dur / 60, 0.1)
    if blink_rate < 10:
        obs.append(f"Blink rate was low ({blink_rate:.1f}/min) — possible screen fatigue or intense focus.")
    elif blink_rate > 30:
        obs.append(f"High blink rate ({blink_rate:.1f}/min) detected — possible drowsiness.")
    else:
        obs.append(f"Blink rate was normal ({blink_rate:.1f}/min).")

    if yawn > 3:
        obs.append(f"{yawn} yawns detected — student may be tired or bored.")
    elif yawn > 0:
        obs.append(f"{yawn} yawn(s) detected — mild fatigue signs.")

    if dist > 5:
        obs.append(f"{dist} distraction events — student frequently looked away from screen.")
    elif dist > 0:
        obs.append(f"{dist} distraction event(s) noted.")
    return obs


def _build_recommendations(avg, yawn, dist):
    recs = []
    if avg < 60:
        recs.append("Consider shorter study sessions with regular 5-minute breaks (Pomodoro technique).")
    if yawn > 2:
        recs.append("Ensure adequate sleep (8-9 hours) before study sessions.")
    if dist > 4:
        recs.append("Study in a quieter environment with fewer visual distractions.")
    recs.append("Keep water nearby and maintain good posture to improve focus.")
    recs.append("Use the 20-20-20 rule: every 20 minutes, look at something 20 feet away for 20 seconds.")
    return recs


def _text_report(stats, avg, dur, blink, yawn, dist, stamp):
    m, s = divmod(int(dur), 60)
    lines = [
        "=== AI Student Attention Report ===",
        f"Generated : {datetime.now().strftime('%d %B %Y  %H:%M:%S')}",
        "",
        f"Average Attention : {avg:.1f}%",
        f"Overall Grade     : {_grade(avg)}",
        f"Session Duration  : {m}m {s}s",
        f"Blink Count       : {blink}",
        f"Yawn Count        : {yawn}",
        f"Distraction Events: {dist}",
        "",
        "Note: Install fpdf2 for a full PDF report.",
    ]
    return "\n".join(lines)
