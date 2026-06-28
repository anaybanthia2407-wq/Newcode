"""
Modern Tkinter dashboard for the AI Student Attention Detection System.

Layout:
  Left  : live webcam feed with landmark overlay
  Right : attention indicator (colour ring + score), live graph, stats panel
  Bottom: control buttons
"""

import tkinter as tk
from tkinter import font as tkfont
import threading
import time
import queue
import math
import os

import cv2
import numpy as np
from PIL import Image, ImageTk
import matplotlib
matplotlib.use("TkAgg")
import matplotlib.pyplot as plt
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
from matplotlib.animation import FuncAnimation

from attention_detector import AttentionDetector
from voice_alert import VoiceAlert
from report_generator import generate_report

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

GRAPH_HISTORY = 300   # data-points to show on live graph (~20 seconds at 15 fps)
ALERT_SECONDS = 10    # voice alert after this many seconds of low attention

# ── Score → colour ─────────────────────────────────────────────────────────────
def score_color(score: float) -> str:
    if score >= 70:
        return GREEN
    if score >= 40:
        return ORANGE
    return RED


def score_label(score: float) -> str:
    if score >= 70:
        return "HIGH"
    if score >= 40:
        return "MEDIUM"
    return "LOW"


# ── Camera thread ───────────────────────────────────────────────────────────────
class CameraThread(threading.Thread):
    def __init__(self, frame_queue: queue.Queue, detector: AttentionDetector,
                 camera_index: int = 0):
        super().__init__(daemon=True)
        self.frame_queue = frame_queue
        self.detector    = detector
        self.running     = True
        self.camera_index = camera_index

    def run(self):
        cap = cv2.VideoCapture(self.camera_index)
        cap.set(cv2.CAP_PROP_FRAME_WIDTH,  640)
        cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 480)
        while self.running:
            ok, frame = cap.read()
            if not ok:
                time.sleep(0.05)
                continue
            frame = cv2.flip(frame, 1)
            annotated, score, face_found = self.detector.process_frame(frame)
            # Drain old frames so we always show the latest
            try:
                self.frame_queue.get_nowait()
            except queue.Empty:
                pass
            self.frame_queue.put((annotated, score, face_found))
        cap.release()

    def stop(self):
        self.running = False


# ── Circular progress indicator ─────────────────────────────────────────────────
class AttentionRing(tk.Canvas):
    """Animated arc that fills based on attention score."""

    SIZE = 180

    def __init__(self, parent, **kwargs):
        super().__init__(parent, width=self.SIZE, height=self.SIZE,
                         bg=BG_CARD, highlightthickness=0, **kwargs)
        self._score = 100.0
        self._draw()

    def set_score(self, score: float):
        self._score = max(0.0, min(100.0, score))
        self._draw()

    def _draw(self):
        self.delete("all")
        s   = self.SIZE
        pad = 18
        col = score_color(self._score)

        # Background ring
        self.create_arc(pad, pad, s - pad, s - pad,
                        start=90, extent=360,
                        outline="#2a2a44", width=14, style="arc")
        # Foreground arc
        extent = (self._score / 100.0) * 360
        if extent > 0:
            self.create_arc(pad, pad, s - pad, s - pad,
                            start=90, extent=-extent,
                            outline=col, width=14, style="arc")

        # Score text
        self.create_text(s // 2, s // 2 - 8, text=f"{self._score:.0f}",
                         fill=col, font=("Helvetica", 34, "bold"))
        self.create_text(s // 2, s // 2 + 22, text=score_label(self._score),
                         fill=col, font=("Helvetica", 11, "bold"))
        self.create_text(s // 2, s // 2 + 36, text="ATTENTION",
                         fill=TEXT_DIM, font=("Helvetica", 8))


# ── Main dashboard window ────────────────────────────────────────────────────────
class Dashboard:
    def __init__(self, root: tk.Tk):
        self.root    = root
        self.detector = AttentionDetector()
        self.alerter  = VoiceAlert()

        self._frame_q    = queue.Queue(maxsize=2)
        self._running    = False
        self._cam_thread: CameraThread | None = None

        self._score_history = [100.0] * GRAPH_HISTORY

        self._build_ui()
        self._start_camera()
        self._update_loop()

    # ── UI construction ────────────────────────────────────────────────────────
    def _build_ui(self):
        self.root.title("AI Student Attention Detection System")
        self.root.configure(bg=BG_DARK)
        self.root.resizable(False, False)

        # ── Title bar ─────────────────────────────────────────────────────────
        title_bar = tk.Frame(self.root, bg=BG_CARD2)
        title_bar.pack(fill="x")

        tk.Label(title_bar,
                 text=" ◉  AI Student Attention Detection",
                 bg=BG_CARD2, fg=ACCENT,
                 font=("Helvetica", 15, "bold"),
                 padx=14, pady=8).pack(side="left")

        self._lbl_time = tk.Label(title_bar, text="00:00",
                                  bg=BG_CARD2, fg=TEXT_DIM,
                                  font=("Helvetica", 12))
        self._lbl_time.pack(side="right", padx=14)

        self._lbl_status = tk.Label(title_bar, text="● STARTING",
                                    bg=BG_CARD2, fg=ORANGE,
                                    font=("Helvetica", 10, "bold"))
        self._lbl_status.pack(side="right", padx=6)

        # ── Main content row ──────────────────────────────────────────────────
        main_row = tk.Frame(self.root, bg=BG_DARK)
        main_row.pack(fill="both", expand=True, padx=10, pady=8)

        # Left: camera feed
        cam_card = tk.Frame(main_row, bg=BG_CARD, bd=0,
                            highlightbackground=ACCENT,
                            highlightthickness=1)
        cam_card.pack(side="left", padx=(0, 8))
        tk.Label(cam_card, text="LIVE FEED", bg=BG_CARD,
                 fg=TEXT_DIM, font=("Helvetica", 8)).pack(pady=(4, 0))
        self._cam_label = tk.Label(cam_card, bg="#000")
        self._cam_label.pack(padx=6, pady=(2, 6))

        # Right panel
        right = tk.Frame(main_row, bg=BG_DARK)
        right.pack(side="left", fill="both", expand=True)

        # Top-right: ring + stats side-by-side
        top_right = tk.Frame(right, bg=BG_DARK)
        top_right.pack(fill="x", pady=(0, 8))

        # Attention ring card
        ring_card = tk.Frame(top_right, bg=BG_CARD,
                             highlightbackground="#2a2a44",
                             highlightthickness=1)
        ring_card.pack(side="left", padx=(0, 8))
        self._ring = AttentionRing(ring_card)
        self._ring.pack(padx=12, pady=12)

        # Stats grid card
        stats_card = tk.Frame(top_right, bg=BG_CARD,
                              highlightbackground="#2a2a44",
                              highlightthickness=1)
        stats_card.pack(side="left", fill="both", expand=True)
        tk.Label(stats_card, text="SESSION STATS", bg=BG_CARD,
                 fg=TEXT_DIM, font=("Helvetica", 8)).pack(pady=(8, 4))

        grid = tk.Frame(stats_card, bg=BG_CARD)
        grid.pack(padx=16, pady=4, fill="both")

        self._stat_vars = {}
        rows = [
            ("Blinks",        "blinks",       "👁"),
            ("Yawns",         "yawns",        "😮"),
            ("Distractions",  "distractions", "↗"),
            ("Session Time",  "session",      "⏱"),
            ("Face",          "face",         "🧑"),
            ("Low Alert",     "alert",        "🔔"),
        ]
        for i, (label, key, icon) in enumerate(rows):
            r, c = divmod(i, 2)
            cell = tk.Frame(grid, bg=BG_CARD2, bd=0,
                            highlightbackground="#2a2a3a", highlightthickness=1)
            cell.grid(row=r, column=c, padx=4, pady=3, sticky="nsew")
            grid.columnconfigure(c, weight=1)

            tk.Label(cell, text=f"{icon}  {label}", bg=BG_CARD2,
                     fg=TEXT_DIM, font=("Helvetica", 8)).pack(anchor="w",
                                                               padx=8, pady=(5, 0))
            var = tk.StringVar(value="—")
            self._stat_vars[key] = var
            tk.Label(cell, textvariable=var, bg=BG_CARD2,
                     fg=TEXT_MAIN, font=("Helvetica", 16, "bold")).pack(
                         anchor="w", padx=8, pady=(0, 5))

        # Live graph card
        graph_card = tk.Frame(right, bg=BG_CARD,
                              highlightbackground="#2a2a44",
                              highlightthickness=1)
        graph_card.pack(fill="both", expand=True, pady=(0, 8))
        tk.Label(graph_card, text="LIVE ATTENTION GRAPH", bg=BG_CARD,
                 fg=TEXT_DIM, font=("Helvetica", 8)).pack(pady=(6, 0))
        self._build_graph(graph_card)

        # ── Bottom buttons ────────────────────────────────────────────────────
        btn_row = tk.Frame(self.root, bg=BG_DARK)
        btn_row.pack(fill="x", padx=10, pady=(0, 10))

        def _btn(text, cmd, colour=ACCENT):
            b = tk.Button(btn_row, text=text, command=cmd,
                          bg=colour, fg="#000", activebackground=colour,
                          font=("Helvetica", 10, "bold"),
                          relief="flat", padx=16, pady=7, cursor="hand2",
                          bd=0, highlightthickness=0)
            b.pack(side="left", padx=5)
            return b

        _btn("⬤  Start", self._start_camera, GREEN)
        _btn("◼  Stop",  self._stop_camera,  RED)
        _btn("📄  Generate Report", self._generate_report, "#3498db")
        _btn("✕  Quit",  self._quit,  "#555")

    # ── Graph ──────────────────────────────────────────────────────────────────
    def _build_graph(self, parent):
        fig, self._ax = plt.subplots(figsize=(5.8, 2.2),
                                     facecolor=BG_CARD)
        self._ax.set_facecolor(BG_CARD2)
        self._line_green, = self._ax.plot([], [], color=GREEN,  lw=1.5)
        self._line_orange, = self._ax.plot([], [], color=ORANGE, lw=1.5)
        self._line_red,   = self._ax.plot([], [], color=RED,    lw=1.5)
        self._ax.axhline(70, color=GREEN,  linestyle="--", lw=0.7, alpha=0.45)
        self._ax.axhline(40, color=RED,    linestyle="--", lw=0.7, alpha=0.45)
        self._ax.set_xlim(0, GRAPH_HISTORY - 1)
        self._ax.set_ylim(0, 105)
        self._ax.set_yticks([0, 40, 70, 100])
        self._ax.tick_params(colors=TEXT_DIM, labelsize=7)
        for sp in self._ax.spines.values():
            sp.set_edgecolor("#2a2a44")
        self._fill_g = self._ax.fill_between([], [], alpha=0)
        self._fill_o = self._ax.fill_between([], [], alpha=0)
        self._fill_r = self._ax.fill_between([], [], alpha=0)
        plt.tight_layout(pad=0.5)

        canvas = FigureCanvasTkAgg(fig, master=parent)
        canvas.draw()
        canvas.get_tk_widget().pack(padx=8, pady=(2, 8), fill="both", expand=True)
        self._graph_canvas = canvas

    def _refresh_graph(self, scores):
        x = list(range(len(scores)))
        y = scores

        # Clear old fills
        [c.remove() for c in self._ax.collections]

        self._ax.set_xlim(0, max(len(scores) - 1, 1))

        # Segment-colour the line
        for i in range(len(y) - 1):
            seg_x = [x[i], x[i + 1]]
            seg_y = [y[i], y[i + 1]]
            clr   = GREEN if y[i] >= 70 else (ORANGE if y[i] >= 40 else RED)
            self._ax.fill_between(seg_x, seg_y, alpha=0.18, color=clr)
            self._ax.plot(seg_x, seg_y, color=clr, lw=1.5)

        self._graph_canvas.draw_idle()

    # ── Camera lifecycle ───────────────────────────────────────────────────────
    def _start_camera(self):
        if self._running:
            return
        self._running = True
        self._cam_thread = CameraThread(self._frame_q, self.detector)
        self._cam_thread.start()
        self._lbl_status.config(text="● RUNNING", fg=GREEN)

    def _stop_camera(self):
        self._running = False
        if self._cam_thread:
            self._cam_thread.stop()
            self._cam_thread = None
        self._lbl_status.config(text="● STOPPED", fg=RED)

    # ── Main update loop (polling via after()) ─────────────────────────────────
    def _update_loop(self):
        try:
            frame, score, face_found = self._frame_q.get_nowait()
        except queue.Empty:
            frame, score, face_found = None, self.detector.current_score, False

        if frame is not None:
            img = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
            img = Image.fromarray(img).resize((520, 390), Image.LANCZOS)
            tk_img = ImageTk.PhotoImage(img)
            self._cam_label.config(image=tk_img)
            self._cam_label.image = tk_img

        # Ring
        self._ring.set_score(score)

        # Score history → graph
        self._score_history.append(score)
        self._score_history = self._score_history[-GRAPH_HISTORY:]
        self._refresh_graph(self._score_history)

        # Stats panel
        d = self.detector
        elapsed = int(time.time() - d.session_start)
        m, s = divmod(elapsed, 60)
        self._stat_vars["blinks"].set(str(d.blink_count))
        self._stat_vars["yawns"].set(str(d.yawn_count))
        self._stat_vars["distractions"].set(str(d.distraction_events))
        self._stat_vars["session"].set(f"{m:02d}:{s:02d}")
        self._stat_vars["face"].set("YES" if face_found else "NO")

        low_dur = d.get_low_attention_duration()
        low_str = f"{low_dur:.0f}s" if low_dur > 0 else "—"
        self._stat_vars["alert"].set(low_str)

        # Title bar clock
        self._lbl_time.config(text=time.strftime("%H:%M:%S"))

        # Voice alert
        if low_dur >= ALERT_SECONDS:
            self.alerter.speak(
                "Attention alert. Please focus on your study material.")

        self.root.after(66, self._update_loop)   # ~15 fps

    # ── Report generation ──────────────────────────────────────────────────────
    def _generate_report(self):
        self._stop_camera()
        stats = self.detector.session_stats()

        def _gen():
            path = generate_report(stats)
            self.root.after(0, lambda: self._show_report_popup(path))

        threading.Thread(target=_gen, daemon=True).start()

    def _show_report_popup(self, path: str):
        win = tk.Toplevel(self.root)
        win.title("Report Generated")
        win.configure(bg=BG_CARD)
        win.resizable(False, False)

        tk.Label(win, text="✓  Report Saved", bg=BG_CARD,
                 fg=GREEN, font=("Helvetica", 14, "bold"),
                 padx=20, pady=14).pack()
        tk.Label(win, text=os.path.abspath(path), bg=BG_CARD,
                 fg=TEXT_DIM, font=("Helvetica", 9),
                 wraplength=380).pack(padx=20, pady=(0, 14))
        tk.Button(win, text="Close", command=win.destroy,
                  bg=ACCENT, fg="#000", font=("Helvetica", 10, "bold"),
                  relief="flat", padx=14, pady=6).pack(pady=(0, 14))

    # ── Quit ──────────────────────────────────────────────────────────────────
    def _quit(self):
        self._stop_camera()
        self.detector.release()
        self.root.destroy()


# ── Entry point ────────────────────────────────────────────────────────────────
def run():
    root = tk.Tk()
    app  = Dashboard(root)
    root.protocol("WM_DELETE_WINDOW", app._quit)
    root.mainloop()


if __name__ == "__main__":
    run()
