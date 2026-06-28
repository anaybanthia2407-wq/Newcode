"""
Flask + Socket.IO backend for the Attention Dashboard.
Python handles ALL AI detection (MediaPipe 468-point face mesh).
Annotated frames + live stats are streamed to the browser via WebSocket.

Usage:
  pip install flask flask-socketio
  python server.py
  Open browser at: http://localhost:5000
"""

import base64
import threading
import time

import cv2
from flask import Flask, send_from_directory
from flask_socketio import SocketIO, emit

from attention_detector import AttentionDetector

app = Flask(__name__, static_folder=".")
app.config["SECRET_KEY"] = "attentionai-sciencefair"
socketio = SocketIO(app, cors_allowed_origins="*", async_mode="threading")

_lock    = threading.Lock()
_running = False


def _stream_loop():
    global _running
    detector = AttentionDetector()
    cap = cv2.VideoCapture(0)
    cap.set(cv2.CAP_PROP_FRAME_WIDTH, 640)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 480)

    try:
        while _running:
            ok, frame = cap.read()
            if not ok:
                time.sleep(0.05)
                continue

            frame = cv2.flip(frame, 1)          # mirror so it looks natural
            annotated, score, face_detected = detector.process_frame(frame)

            # Encode annotated frame as JPEG for browser
            _, buf = cv2.imencode(".jpg", annotated, [cv2.IMWRITE_JPEG_QUALITY, 70])
            b64 = base64.b64encode(buf).decode()

            socketio.emit("data", {
                "frame":        b64,
                "score":        round(score, 1),
                "face":         face_detected,
                "blinks":       detector.blink_count,
                "yawns":        detector.yawn_count,
                "distractions": detector.distraction_events,
                "low_secs":     round(detector.get_low_attention_duration(), 1),
                "ear":          round(detector.last_ear,   3),
                "mar":          round(detector.last_mar,   3),
                "yaw":          round(detector.last_yaw,   1),
                "pitch":        round(detector.last_pitch, 1),
            })

            time.sleep(0.066)   # ~15 fps
    finally:
        cap.release()
        detector.release()


@app.route("/")
def index():
    return send_from_directory(".", "index.html")


@socketio.on("start")
def on_start():
    global _running
    with _lock:
        if not _running:
            _running = True
            threading.Thread(target=_stream_loop, daemon=True).start()
    emit("status", {"msg": "Python AI camera started"})


@socketio.on("stop")
def on_stop():
    global _running
    with _lock:
        _running = False
    emit("status", {"msg": "Camera stopped"})


if __name__ == "__main__":
    print("\n  AttentionAI — Python Backend Server")
    print("  MediaPipe 468-point face mesh active")
    print("  Open your browser at: http://localhost:5000\n")
    socketio.run(app, host="0.0.0.0", port=5000, debug=False)
