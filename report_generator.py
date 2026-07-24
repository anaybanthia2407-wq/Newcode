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

try:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    import numpy as np
    _MPL_AVAILABLE = True
except ImportError:
    _MPL_AVAILABLE = False


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

    if _MPL_AVAILABLE and sc:
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
        pdf.multi_cell(180, 6, f">>  {obs}")

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
        pdf.multi_cell(180, 6, f"[+]  {rec}")

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


# ── Weekly summary report ──────────────────────────────────────────────────────
def generate_weekly_report(sessions: list) -> str:
    """
    Generate a PDF summarising all sessions from the last 7 days.
    sessions: list of dicts from get_all_sessions().
    Returns path to saved PDF.
    """
    os.makedirs(REPORT_DIR, exist_ok=True)
    stamp      = datetime.now().strftime("%Y%m%d_%H%M%S")
    pdf_path   = os.path.join(REPORT_DIR, f"weekly_report_{stamp}.pdf")
    graph_path = os.path.join(REPORT_DIR, f"_tmp_weekly_{stamp}.png")

    valid = [s for s in sessions if s.get("avg_score") is not None]
    dates = []
    avgs  = []
    for s in valid:
        try:
            dt = datetime.fromisoformat(s["start_time"])
            dates.append(dt.strftime("%a %d"))
            avgs.append(s["avg_score"])
        except Exception:
            pass

    # Weekly trend chart
    if _MPL_AVAILABLE:
        fig, ax = plt.subplots(figsize=(9, 3), facecolor="#1a1a2e")
        ax.set_facecolor("#16213e")
        if avgs:
            colours = ["#2ecc71" if a >= 70 else "#f39c12" if a >= 40 else "#e74c3c"
                       for a in avgs]
            ax.bar(range(len(avgs)), avgs, color=colours, width=0.6, zorder=3)
            ax.axhline(70, color="#2ecc71", linestyle="--", lw=0.8, alpha=0.5)
            ax.axhline(40, color="#e74c3c", linestyle="--", lw=0.8, alpha=0.5)
            ax.set_xticks(range(len(avgs)))
            ax.set_xticklabels(dates, color="white", fontsize=7)
        ax.set_ylim(0, 105)
        ax.set_ylabel("Avg Attention %", color="white", fontsize=8)
        ax.set_title("Weekly Attention Summary", color="white", fontsize=10, pad=6)
        ax.tick_params(colors="white", labelsize=7)
        for sp in ax.spines.values():
            sp.set_edgecolor("#444")
        plt.tight_layout()
        fig.savefig(graph_path, dpi=130, facecolor=fig.get_facecolor())
        plt.close(fig)

    weekly_avg   = sum(avgs) / len(avgs) if avgs else 0.0
    total_blinks = sum(s.get("total_blinks",       0) or 0 for s in sessions)
    total_yawns  = sum(s.get("total_yawns",        0) or 0 for s in sessions)
    total_dist   = sum(s.get("total_distractions", 0) or 0 for s in sessions)

    if not _FPDF_AVAILABLE:
        txt_path = pdf_path.replace(".pdf", ".txt")
        with open(txt_path, "w") as f:
            f.write(f"=== Weekly Attention Report ===\n")
            f.write(f"Period: last 7 days  |  Sessions: {len(sessions)}\n\n")
            f.write(f"Weekly Average Attention : {weekly_avg:.1f}%\n")
            f.write(f"Grade                    : {_grade(weekly_avg)}\n")
            f.write(f"Total Blinks             : {total_blinks}\n")
            f.write(f"Total Yawns              : {total_yawns}\n")
            f.write(f"Total Distractions       : {total_dist}\n")
        if os.path.exists(graph_path):
            os.remove(graph_path)
        return txt_path

    pdf = FPDF()
    pdf.set_auto_page_break(auto=True, margin=15)
    pdf.add_page()

    # Header
    pdf.set_fill_color(22, 33, 62)
    pdf.rect(0, 0, 210, 40, "F")
    pdf.set_text_color(46, 204, 113)
    pdf.set_font("Helvetica", "B", 20)
    pdf.set_xy(10, 8)
    pdf.cell(0, 10, "Weekly Attention Summary", ln=True)
    pdf.set_font("Helvetica", "", 10)
    pdf.set_text_color(180, 180, 200)
    pdf.set_x(10)
    pdf.cell(0, 6,
             f"Generated: {datetime.now().strftime('%d %B %Y')}  |  "
             f"Sessions this week: {len(sessions)}", ln=True)
    pdf.ln(8)

    # Summary row
    pdf.set_fill_color(26, 26, 46)
    pdf.set_draw_color(46, 204, 113)
    pdf.set_line_width(0.5)
    pdf.rect(10, pdf.get_y(), 190, 40, "FD")
    y0 = pdf.get_y() + 4

    def _cell(x, y, label, val, col):
        pdf.set_xy(x, y)
        pdf.set_text_color(*col)
        pdf.set_font("Helvetica", "B", 20)
        pdf.cell(45, 8, str(val))
        pdf.set_xy(x, y + 10)
        pdf.set_text_color(150, 150, 170)
        pdf.set_font("Helvetica", "", 8)
        pdf.cell(45, 5, label)

    avg_col = (46,204,113) if weekly_avg >= 70 else (243,156,18) if weekly_avg >= 40 else (231,76,60)
    _cell(15,  y0, "WEEKLY AVG",    f"{weekly_avg:.1f}%", avg_col)
    _cell(65,  y0, "SESSIONS",      str(len(sessions)),    (100,180,255))
    _cell(115, y0, "TOTAL BLINKS",  str(total_blinks),     (160,100,255))
    _cell(160, y0, "DISTRACTIONS",  str(total_dist),       (231,76,60))

    pdf.ln(50)

    # Chart
    pdf.set_text_color(200,200,220)
    pdf.set_font("Helvetica", "B", 12)
    pdf.set_x(10)
    pdf.cell(0, 8, "Daily Attention Scores", ln=True)
    if os.path.exists(graph_path):
        pdf.image(graph_path, x=10, w=190)
    pdf.ln(4)

    # Per-session table
    if sessions:
        pdf.set_font("Helvetica", "B", 11)
        pdf.set_text_color(200,200,220)
        pdf.cell(0, 8, "Session Log", ln=True)
        pdf.set_font("Helvetica", "B", 8)
        pdf.set_fill_color(30, 40, 70)
        pdf.set_text_color(150, 200, 255)
        for header, w in [("Date/Time", 60), ("Avg %", 25), ("Grade", 30),
                           ("Blinks", 22), ("Yawns", 20), ("Distractions", 33)]:
            pdf.cell(w, 6, header, fill=True)
        pdf.ln()
        pdf.set_font("Helvetica", "", 8)
        for i, s in enumerate(sessions):
            try:
                dt = datetime.fromisoformat(s["start_time"]).strftime("%d %b %Y %H:%M")
            except Exception:
                dt = s.get("start_time", "—")
            avg_s = s.get("avg_score") or 0
            col = (46,204,113) if avg_s >= 70 else (243,156,18) if avg_s >= 40 else (231,76,60)
            fill = i % 2 == 0
            pdf.set_fill_color(20, 28, 50) if fill else pdf.set_fill_color(16, 22, 40)
            pdf.set_text_color(200, 200, 220)
            pdf.cell(60, 5, dt, fill=True)
            pdf.set_text_color(*col)
            pdf.cell(25, 5, f"{avg_s:.1f}%", fill=True)
            pdf.set_text_color(180, 180, 200)
            pdf.cell(30, 5, _grade(avg_s), fill=True)
            pdf.cell(22, 5, str(s.get("total_blinks",       0) or 0), fill=True)
            pdf.cell(20, 5, str(s.get("total_yawns",        0) or 0), fill=True)
            pdf.cell(33, 5, str(s.get("total_distractions", 0) or 0), fill=True)
            pdf.ln()

    # Footer
    pdf.set_y(-18)
    pdf.set_font("Helvetica", "I", 8)
    pdf.set_text_color(100, 100, 120)
    pdf.cell(0, 6, "AI Student Attention Detection System  |  Weekly Report", align="C")

    pdf.output(pdf_path)
    if os.path.exists(graph_path):
        os.remove(graph_path)
    return pdf_path
