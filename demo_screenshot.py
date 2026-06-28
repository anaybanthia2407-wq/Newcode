"""
Renders the full dashboard with simulated attention data and saves a screenshot.
No webcam required — uses synthetic score history to populate all widgets.
"""

import time
import math
import random
import os
import sys

os.environ["DISPLAY"] = ":99"

import tkinter as tk
import numpy as np
import matplotlib
matplotlib.use("TkAgg")

# Simulate realistic attention data
random.seed(42)
np.random.seed(42)

# ── Patch AttentionDetector to not need a camera ──────────────────────────────
import attention_detector as _ad

class FakeDetector:
    """Supplies synthetic session data for the screenshot."""
    blink_count        = 34
    yawn_count         = 3
    distraction_events = 7
    current_score      = 72.0
    face_detected      = True
    session_start      = time.time() - 427   # 7 min 7 sec ago

    def __init__(self):
        # Generate a realistic 300-point attention trace
        t = np.linspace(0, 420, 300)
        base = 75 + 15 * np.sin(t / 60) + np.random.normal(0, 6, 300)
        # add a low-attention dip around t=180
        dip = np.exp(-((t - 180) ** 2) / (2 * 30 ** 2)) * 40
        raw = np.clip(base - dip, 0, 100)
        self.score_history     = list(raw)
        self.timestamp_history = list(t)

    def get_low_attention_duration(self):
        return 0.0

    def session_stats(self):
        return {
            "duration_seconds":   427,
            "average_attention":  float(np.mean(self.score_history)),
            "blink_count":        self.blink_count,
            "yawn_count":         self.yawn_count,
            "distraction_events": self.distraction_events,
            "score_history":      self.score_history,
            "timestamp_history":  self.timestamp_history,
        }

    def release(self): pass


# ── Monkey-patch before importing dashboard ───────────────────────────────────
import dashboard as _db
_db.AttentionDetector = FakeDetector

# Also patch CameraThread to do nothing
class _FakeCamThread:
    def __init__(self, *a, **kw): pass
    def start(self): pass
    def stop(self):  pass

_db.CameraThread = _FakeCamThread

# ── Build & screenshot ─────────────────────────────────────────────────────────
root = tk.Tk()

class DemoApp(_db.Dashboard):
    def __init__(self, root):
        # Directly call parent __init__ but override detector
        self.root     = root
        self.detector = FakeDetector()
        from voice_alert import VoiceAlert
        self.alerter  = VoiceAlert()
        import queue
        self._frame_q   = queue.Queue(maxsize=2)
        self._running   = False
        self._cam_thread = None
        self._score_history = list(self.detector.score_history)
        self._build_ui()
        # Pre-populate the graph with full history
        self._refresh_graph(self._score_history)
        # Populate all stat labels
        self._stat_vars["blinks"].set("34")
        self._stat_vars["yawns"].set("3")
        self._stat_vars["distractions"].set("7")
        self._stat_vars["session"].set("07:07")
        self._stat_vars["face"].set("YES")
        self._stat_vars["alert"].set("—")
        self._ring.set_score(72.0)
        self._lbl_status.config(text="● RUNNING", fg=_db.GREEN)
        self._lbl_time.config(text="14:32:18")

    def _start_camera(self): pass
    def _stop_camera(self):  pass
    def _update_loop(self):  pass   # no polling loop needed for screenshot

app = DemoApp(root)

# Give Tk a moment to render everything
root.update()
root.update_idletasks()
root.after(800, lambda: None)
root.update()

# Screenshot via PIL
from PIL import ImageGrab
import subprocess

out = "/home/user/Newcode/dashboard_preview.png"

# Use xwd + convert for headless X screenshot
try:
    root.update()
    # Get window geometry
    root.update_idletasks()
    w = root.winfo_width()
    h = root.winfo_height()
    x = root.winfo_rootx()
    y = root.winfo_rooty()
    print(f"Window size: {w}x{h} at ({x},{y})")

    # Use scrot or import (ImageMagick)
    result = subprocess.run(
        ["import", "-window", "root", out],
        capture_output=True, timeout=5
    )
    if result.returncode != 0:
        raise RuntimeError(result.stderr.decode())
    print(f"Screenshot saved: {out}")
except Exception as e:
    print(f"Screenshot method 1 failed: {e}")
    # Fallback: render to PNG via matplotlib figure of the whole UI
    try:
        import matplotlib.pyplot as plt
        fig = plt.figure(figsize=(13, 8), facecolor="#0d0d1a")
        plt.text(0.5, 0.5, "Dashboard rendered — run python main.py to see live",
                 ha="center", va="center", color="white", fontsize=14)
        fig.savefig(out)
        plt.close()
        print(f"Fallback image saved: {out}")
    except Exception as e2:
        print(f"Fallback also failed: {e2}")

root.destroy()
