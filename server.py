"""
Flask + Socket.IO backend for the Attention Dashboard.
Python handles ALL AI detection (MediaPipe 468-point face mesh).

Features:
  - Streams annotated frames + live stats to browser via WebSocket
  - Logs every movement frame to SQLite database (database.py)
  - Auto-screenshot saved to screenshots/ when score < 50
  - Face recognition via LBPH model (face_model.yml) if available

Usage:
  python server.py
  Open browser at: http://localhost:5000
"""

import base64
import os
import threading
import time
import json
from datetime import datetime

import cv2
from flask import Flask, send_from_directory, jsonify
from flask_socketio import SocketIO, emit

from attention_detector import AttentionDetector
from database import SessionDB, get_all_sessions, get_session_movements, get_session_screenshots

# ── Setup ──────────────────────────────────────────────────────────────────────
app = Flask(__name__, static_folder=".")
app.config["SECRET_KEY"] = "attentionai-sciencefair"
socketio = SocketIO(app, cors_allowed_origins="*", async_mode="threading")

SCREENSHOT_DIR   = "screenshots"
SCREENSHOT_SCORE = 50          # take screenshot when score drops below this
MODEL_FILE       = "face_model.yml"
LABELS_FILE      = "face_labels.json"

os.makedirs(SCREENSHOT_DIR, exist_ok=True)

_lock          = threading.Lock()
_running       = False
_session_db    = None
_student_name  = "Student"

# ── Face recognition (optional) ────────────────────────────────────────────────
_recogniser  = None
_label_map   = {}
_face_cascade = cv2.CascadeClassifier(
    cv2.data.haarcascades + "haarcascade_frontalface_default.xml"
)

def _load_face_model():
    global _recogniser, _label_map
    if not os.path.exists(MODEL_FILE):
        return
    try:
        rec = cv2.face.LBPHFaceRecognizer_create()
        rec.read(MODEL_FILE)
        _recogniser = rec
        if os.path.exists(LABELS_FILE):
            with open(LABELS_FILE) as f:
                _label_map = {int(k): v for k, v in json.load(f).items()}
        print(f"  Face model loaded: {len(_label_map)} student(s) — {list(_label_map.values())}")
    except AttributeError:
        print("  Face recognition disabled (opencv-contrib-python not installed).")


def _recognise_face(gray_frame):
    """Return (name, confidence) for the most prominent face, or (None, 0)."""
    if _recogniser is None:
        return None, 0
    faces = _face_cascade.detectMultiScale(gray_frame, scaleFactor=1.3, minNeighbors=5)
    best_name, best_conf = None, 999
    for (x, y, w, h) in faces:
        roi  = cv2.resize(gray_frame[y:y+h, x:x+w], (100, 100))
        sid, conf = _recogniser.predict(roi)
        if conf < best_conf:
            best_conf = conf
            best_name = _label_map.get(sid, "Unknown")
    return (best_name, best_conf) if best_conf < 80 else (None, best_conf)


# ── Screenshot helper ──────────────────────────────────────────────────────────
_last_screenshot_time = 0
SCREENSHOT_COOLDOWN   = 10    # seconds between screenshots

def _maybe_screenshot(frame, score, session_db: SessionDB):
    global _last_screenshot_time
    now = time.time()
    if score < SCREENSHOT_SCORE and (now - _last_screenshot_time) >= SCREENSHOT_COOLDOWN:
        _last_screenshot_time = now
        stamp    = datetime.now().strftime("%Y%m%d_%H%M%S")
        filename = f"low_attention_{stamp}_score{int(score)}.jpg"
        filepath = os.path.join(SCREENSHOT_DIR, filename)
        cv2.imwrite(filepath, frame)
        session_db.log_screenshot(score, filepath)
        socketio.emit("screenshot", {
            "path":  filepath,
            "score": round(score, 1),
            "time":  stamp,
        })
        print(f"  Screenshot saved: {filepath}  (score={score:.1f})")


# ── Main detection loop ────────────────────────────────────────────────────────
def _stream_loop(student_name: str):
    global _running, _session_db

    detector    = AttentionDetector()
    session_db  = SessionDB(student_name)
    _session_db = session_db

    cap = cv2.VideoCapture(0)
    cap.set(cv2.CAP_PROP_FRAME_WIDTH, 640)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 480)

    frame_count  = 0
    recognised   = student_name   # start with provided name; update via face recog

    try:
        while _running:
            ok, frame = cap.read()
            if not ok:
                time.sleep(0.05)
                continue

            frame    = cv2.flip(frame, 1)
            annotated, score, face_detected = detector.process_frame(frame)

            # ── Face recognition (every 30 frames to save CPU) ────────────────
            frame_count += 1
            if frame_count % 30 == 0 and face_detected:
                gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
                name, conf = _recognise_face(gray)
                if name:
                    recognised = name
                    cv2.putText(annotated, f"{name} ({conf:.0f})",
                                (10, annotated.shape[0] - 10),
                                cv2.FONT_HERSHEY_SIMPLEX, 0.6, (0, 210, 80), 2)

            # ── Screenshot on low attention ───────────────────────────────────
            _maybe_screenshot(frame, score, session_db)

            # ── Log movement to database ──────────────────────────────────────
            event = None
            if detector.blink_count  > 0 and detector.blink_count  % 1 == 0: event = "blink"
            if detector.yawn_count   > 0 and not event: event = "yawn"
            if detector.distraction_events > 0 and not event: event = "distraction"

            session_db.log_movement(
                timestamp = time.time() - detector.session_start,
                score     = score,
                ear       = detector.last_ear,
                mar       = detector.last_mar,
                yaw       = detector.last_yaw,
                pitch     = detector.last_pitch,
                event     = event,
            )

            # ── Encode & stream frame to browser ──────────────────────────────
            _, buf = cv2.imencode(".jpg", annotated, [cv2.IMWRITE_JPEG_QUALITY, 70])
            b64    = base64.b64encode(buf).decode()

            socketio.emit("data", {
                "frame":        b64,
                "score":        round(score, 1),
                "face":         face_detected,
                "recognised":   recognised,
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
        stats = detector.session_stats()
        session_db.end_session(
            avg_score    = stats["average_attention"],
            blinks       = detector.blink_count,
            yawns        = detector.yawn_count,
            distractions = detector.distraction_events,
        )
        cap.release()
        detector.release()
        print(f"  Session ended for {student_name}. Data saved to database.")


# ── Flask routes ───────────────────────────────────────────────────────────────
@app.route("/")
def index():
    return send_from_directory(".", "index.html")


@app.route("/api/sessions")
def api_sessions():
    return jsonify(get_all_sessions())


@app.route("/api/sessions/<int:sid>/movements")
def api_movements(sid):
    return jsonify(get_session_movements(sid))


@app.route("/api/sessions/<int:sid>/screenshots")
def api_screenshots(sid):
    return jsonify(get_session_screenshots(sid))


# ── Socket.IO events ───────────────────────────────────────────────────────────
@socketio.on("start")
def on_start(data=None):
    global _running, _student_name
    name = (data or {}).get("name", "Student").strip() or "Student"
    _student_name = name
    with _lock:
        if not _running:
            _running = True
            threading.Thread(target=_stream_loop, args=(name,), daemon=True).start()
    emit("status", {"msg": f"Python AI started — session for {name}"})


@socketio.on("stop")
def on_stop():
    global _running
    with _lock:
        _running = False
    emit("status", {"msg": "Camera stopped — session saved to database"})


# ── Entry point ────────────────────────────────────────────────────────────────
if __name__ == "__main__":
    _load_face_model()
    print("\n  AttentionAI — Python Backend Server")
    print("  MediaPipe 468-point face mesh active")
    print(f"  Screenshots saved to: {SCREENSHOT_DIR}/  (when score < {SCREENSHOT_SCORE})")
    print("  Database: attention_data.db")
    print("  Open your browser at: http://localhost:5000\n")
    socketio.run(app, host="0.0.0.0", port=5000, debug=False)
