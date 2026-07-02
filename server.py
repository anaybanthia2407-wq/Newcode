"""
Flask + Socket.IO backend for the Attention Dashboard.
Python handles ALL AI detection (MediaPipe 468-point face mesh).

Features:
  - Streams annotated frames + live stats to browser via WebSocket
  - Logs every movement frame to SQLite database
  - Auto-screenshot saved to screenshots/ when score < 50
  - Face recognition via LBPH model (face_model.yml) if available

Usage:
  py -3.11 server.py
  Open browser at: http://localhost:5000
"""

import base64
import os
import threading
import time
import json
from datetime import datetime

import cv2
import mediapipe as mp
from flask import Flask, send_from_directory, jsonify
from flask_socketio import SocketIO, emit

from attention_detector import AttentionDetector
from database import SessionDB, get_all_sessions, get_session_movements, get_session_screenshots

# ── App setup ──────────────────────────────────────────────────────────────────
app = Flask(__name__, static_folder=".")
app.config["SECRET_KEY"] = "attentionai-sciencefair"
socketio = SocketIO(app, cors_allowed_origins="*", async_mode="threading")

SCREENSHOT_DIR   = "screenshots"
SCREENSHOT_SCORE = 50
MODEL_FILE       = "face_model.yml"
LABELS_FILE      = "face_labels.json"

os.makedirs(SCREENSHOT_DIR, exist_ok=True)

_lock         = threading.Lock()
_running      = False

# ── Face recognition (optional, uses MediaPipe-based LBPH model) ───────────────
_recogniser = None
_label_map  = {}

# MediaPipe face detection for recognition (no Haar cascade needed)
_mp_face_det   = mp.solutions.face_detection
_face_detector = _mp_face_det.FaceDetection(
    model_selection=0, min_detection_confidence=0.6
)


def _load_face_model():
    global _recogniser, _label_map
    if not os.path.exists(MODEL_FILE):
        print("  No face model found — face recognition disabled.")
        print("  Run: py -3.11 face_trainer.py  to enroll students.")
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
    except Exception as e:
        print(f"  Face model load error: {e}")


def _recognise_face(frame):
    """Use MediaPipe to detect face, then LBPH to recognise. Returns name or None."""
    if _recogniser is None:
        return None
    h, w = frame.shape[:2]
    rgb  = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
    res  = _face_detector.process(rgb)
    if not res.detections:
        return None
    det  = res.detections[0].location_data.relative_bounding_box
    x1   = max(0, int(det.xmin * w))
    y1   = max(0, int(det.ymin * h))
    x2   = min(w, int((det.xmin + det.width) * w))
    y2   = min(h, int((det.ymin + det.height) * h))
    if x2 <= x1 or y2 <= y1:
        return None
    gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
    roi  = cv2.resize(gray[y1:y2, x1:x2], (100, 100))
    sid, conf = _recogniser.predict(roi)
    return _label_map.get(sid) if conf < 80 else None


# ── Camera open helper ─────────────────────────────────────────────────────────
def _open_camera():
    """Try camera indices 0, 1, 2 and return the first one that works."""
    for idx in range(3):
        cap = cv2.VideoCapture(idx)
        if cap.isOpened():
            ret, _ = cap.read()
            if ret:
                cap.set(cv2.CAP_PROP_FRAME_WIDTH, 640)
                cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 480)
                print(f"  Camera opened on index {idx}")
                return cap
        cap.release()
    return None


# ── Screenshot helper ──────────────────────────────────────────────────────────
_last_screenshot_time = 0
SCREENSHOT_COOLDOWN   = 10

def _maybe_screenshot(frame, score, session_db):
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
            "path": filepath, "score": round(score, 1), "time": stamp
        })
        print(f"  Screenshot: {filepath}  (score={score:.0f})")


# ── Main detection loop ────────────────────────────────────────────────────────
def _stream_loop(student_name: str):
    global _running

    # Open webcam
    cap = _open_camera()
    if cap is None:
        socketio.emit("camera_error", {
            "msg": "Cannot open webcam. Make sure no other app (Zoom, Teams) is using it."
        })
        print("  ERROR: Could not open any camera.")
        with _lock:
            _running = False
        return

    detector   = AttentionDetector()
    session_db = SessionDB(student_name)
    recognised = student_name
    frame_count = 0

    try:
        while _running:
            ok, frame = cap.read()
            if not ok:
                time.sleep(0.05)
                continue

            frame = cv2.flip(frame, 1)
            annotated, score, face_detected = detector.process_frame(frame)

            # Face recognition every 30 frames
            frame_count += 1
            if frame_count % 30 == 0 and face_detected:
                name = _recognise_face(frame)
                if name:
                    recognised = name

            # Auto screenshot on low attention
            _maybe_screenshot(frame, score, session_db)

            # Log movement to database
            session_db.log_movement(
                timestamp = time.time() - detector.session_start,
                score     = score,
                ear       = detector.last_ear,
                mar       = detector.last_mar,
                yaw       = detector.last_yaw,
                pitch     = detector.last_pitch,
            )

            # Encode frame as JPEG and send to browser
            ok_enc, buf = cv2.imencode(".jpg", annotated, [cv2.IMWRITE_JPEG_QUALITY, 70])
            if not ok_enc or buf is None or len(buf) == 0:
                time.sleep(0.066)
                continue
            b64 = base64.b64encode(buf.tobytes()).decode()

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
        print(f"  Session ended for {student_name}.")


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
    global _running
    name = (data or {}).get("name", "Student").strip() or "Student"
    with _lock:
        if not _running:
            _running = True
            threading.Thread(target=_stream_loop, args=(name,), daemon=True).start()
    emit("status", {"msg": f"Starting Python AI for {name}..."})

@socketio.on("stop")
def on_stop():
    global _running
    with _lock:
        _running = False
    emit("status", {"msg": "Camera stopped — session saved."})


# ── Entry point ────────────────────────────────────────────────────────────────
if __name__ == "__main__":
    _load_face_model()
    print("\n  AttentionAI — Python Backend Server")
    print("  MediaPipe 468-point face mesh active")
    print(f"  Screenshots folder: {SCREENSHOT_DIR}/")
    print("  Database: attention_data.db")
    print("  Open your browser at: http://localhost:5000\n")
    socketio.run(app, host="0.0.0.0", port=5000, debug=False)
