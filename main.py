#!/usr/bin/env python3
"""
AI Student Attention Detection System
======================================
Science Fair Project — Class 10

Run:
    python main.py

Dependencies (install once):
    pip install -r requirements.txt
"""

import sys


def _check_deps():
    missing = []
    for pkg in ("cv2", "mediapipe", "numpy", "PIL", "matplotlib", "scipy"):
        try:
            __import__(pkg)
        except ImportError:
            missing.append(pkg)
    if missing:
        print(f"[ERROR] Missing packages: {', '.join(missing)}")
        print("Run:  pip install -r requirements.txt")
        sys.exit(1)


if __name__ == "__main__":
    _check_deps()
    from dashboard import run
    run()
