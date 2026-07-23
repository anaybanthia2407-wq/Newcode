"""
Flask + Socket.IO backend for the Attention Dashboard.
Python handles ALL AI detection (MediaPipe 468-point face mesh).

Usage:
  py -3.11 server.py
  Open browser at: http://localhost:5000
"""

import base64
import csv
import io
import json
import os
import threading
import time
from datetime import datetime, timedelta

import cv2
import mediapipe as mp
from flask import Flask, Response, jsonify, make_response, send_from_directory
from flask_socketio import SocketIO, emit

from attention_detector import AttentionDetector
from database import (DB_PATH, SessionDB, get_all_sessions,
                      get_session_movements, get_session_screenshots, init_db)
from report_generator import generate_report, generate_weekly_report
from voice_alert import VoiceAlert

# ── App setup ──────────────────────────────────────────────────────────────────
app = Flask(__name__, static_folder=".")
app.config["SECRET_KEY"] = "attentionai-sciencefair"
socketio = SocketIO(app, cors_allowed_origins="*", async_mode="threading")

SCREENSHOT_DIR   = "screenshots"
SCREENSHOT_SCORE = 50
MODEL_FILE       = "face_model.yml"
LABELS_FILE      = "face_labels.json"

os.makedirs(SCREENSHOT_DIR, exist_ok=True)

_lock       = threading.Lock()
_running    = False
_paused     = False
_alert_mode = "voice"   # "voice" | "beep" | "silent"
_alerter    = VoiceAlert(mode=_alert_mode)

# ── Face recognition (optional) ────────────────────────────────────────────────
_recogniser = None
_label_map  = {}

_mp_face_det   = mp.solutions.face_detection
_face_detector = _mp_face_det.FaceDetection(model_selection=0, min_detection_confidence=0.6)


def _load_face_model():
    global _recogniser, _label_map
    if not os.path.exists(MODEL_FILE):
        print("  No face model found — recognition disabled.")
        return
    try:
        rec = cv2.face.LBPHFaceRecognizer_create()
        rec.read(MODEL_FILE)
        _recogniser = rec
        if os.path.exists(LABELS_FILE):
            with open(LABELS_FILE) as f:
                _label_map = {int(k): v for k, v in json.load(f).items()}
        print(f"  Face model loaded: {list(_label_map.values())}")
    except AttributeError:
        print("  Face recognition disabled (opencv-contrib-python not installed).")
    except Exception as e:
        print(f"  Face model load error: {e}")


def _recognise_face(frame):
    if _recogniser is None:
        return None
    h, w = frame.shape[:2]
    rgb  = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
    res  = _face_detector.process(rgb)
    if not res.detections:
        return None
    det  = res.detections[0].location_data.relative_bounding_box
    x1 = max(0, int(det.xmin * w))
    y1 = max(0, int(det.ymin * h))
    x2 = min(w, int((det.xmin + det.width)  * w))
    y2 = min(h, int((det.ymin + det.height) * h))
    if x2 <= x1 or y2 <= y1:
        return None
    gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
    roi  = cv2.resize(gray[y1:y2, x1:x2], (100, 100))
    sid, conf = _recogniser.predict(roi)
    return _label_map.get(sid) if conf < 80 else None


# ── Camera open helper ─────────────────────────────────────────────────────────
def _open_camera():
    for idx in range(3):
        cap = cv2.VideoCapture(idx)
        if cap.isOpened():
            ret, _ = cap.read()
            if ret:
                cap.set(cv2.CAP_PROP_FRAME_WIDTH,  640)
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
            "path": filepath, "filename": filename,
            "score": round(score, 1), "time": stamp,
        })
        print(f"  Screenshot: {filepath}  (score={score:.0f})")


# ── Main detection loop ────────────────────────────────────────────────────────
def _stream_loop(student_name: str):
    global _running

    cap = _open_camera()
    if cap is None:
        socketio.emit("camera_error", {
            "msg": "Cannot open webcam. Make sure no other app (Zoom, Teams) is using it."
        })
        with _lock:
            _running = False
        return

    detector    = AttentionDetector()
    session_db  = SessionDB(student_name)
    recognised  = student_name
    frame_count = 0
    low_alert_cooldown = 0

    try:
        while _running:
            ok, frame = cap.read()
            if not ok:
                time.sleep(0.05)
                continue

            frame = cv2.flip(frame, 1)
            annotated, score, face_detected = detector.process_frame(frame)

            frame_count += 1
            if frame_count % 30 == 0 and face_detected:
                name = _recognise_face(frame)
                if name:
                    recognised = name

            if not _paused:
                _maybe_screenshot(frame, score, session_db)

                # Voice / beep alert on low attention (server-side)
                low_secs = detector.get_low_attention_duration()
                now = time.time()
                if low_secs >= 10 and now > low_alert_cooldown:
                    low_alert_cooldown = now + 15
                    _alerter.speak("Attention alert. Please focus on your study material.")

                # Drowsiness alert
                if detector.is_drowsy:
                    _alerter.speak("Drowsiness detected. Please wake up and focus.")

            session_db.log_movement(
                timestamp = time.time() - detector.session_start,
                score     = score,
                ear       = detector.last_ear,
                mar       = detector.last_mar,
                yaw       = detector.last_yaw,
                pitch     = detector.last_pitch,
                gaze_dir  = detector.gaze_direction,
                posture   = detector.last_posture,
            )

            ok_enc, buf = cv2.imencode(".jpg", annotated, [cv2.IMWRITE_JPEG_QUALITY, 70])
            if not ok_enc or buf is None or len(buf) == 0:
                time.sleep(0.066)
                continue
            b64 = base64.b64encode(buf.tobytes()).decode()

            socketio.emit("data", {
                "frame":          b64,
                "score":          round(score, 1),
                "face":           face_detected,
                "recognised":     recognised,
                "blinks":         detector.blink_count,
                "yawns":          detector.yawn_count,
                "distractions":   detector.distraction_events,
                "drowsy_events":  detector.drowsy_events,
                "posture_events": detector.posture_events,
                "low_secs":       round(detector.get_low_attention_duration(), 1),
                "ear":            round(detector.last_ear,        3),
                "mar":            round(detector.last_mar,        3),
                "yaw":            round(detector.last_yaw,        1),
                "pitch":          round(detector.last_pitch,      1),
                "gaze_dir":       detector.gaze_direction,
                "gaze_x":         round(detector.last_gaze_x,    3),
                "gaze_y":         round(detector.last_gaze_y,    3),
                "posture":        detector.last_posture,
                "trend":          round(detector.attention_trend, 1),
                "drowsy":         detector.is_drowsy,
            })

            time.sleep(0.066)   # ~15 fps

    finally:
        stats = detector.session_stats()
        session_db.end_session(
            avg_score      = stats["average_attention"],
            blinks         = detector.blink_count,
            yawns          = detector.yawn_count,
            distractions   = detector.distraction_events,
            drowsy         = detector.drowsy_events,
            posture_events = detector.posture_events,
        )
        cap.release()
        detector.release()
        print(f"  Session ended for {student_name}.")


# ── Flask routes ───────────────────────────────────────────────────────────────
@app.route("/")
def index():
    return send_from_directory(".", "index.html")

@app.route("/screenshots/<path:filename>")
def serve_screenshot(filename):
    return send_from_directory(SCREENSHOT_DIR, filename)

@app.route("/api/sessions")
def api_sessions():
    return jsonify(get_all_sessions())

@app.route("/api/sessions/<int:sid>/movements")
def api_movements(sid):
    return jsonify(get_session_movements(sid))

@app.route("/api/sessions/<int:sid>/screenshots")
def api_screenshots(sid):
    return jsonify(get_session_screenshots(sid))

@app.route("/api/sessions/<int:sid>/csv")
def api_session_csv(sid):
    rows = get_session_movements(sid)
    buf  = io.StringIO()
    if rows:
        writer = csv.DictWriter(buf, fieldnames=rows[0].keys())
        writer.writeheader()
        writer.writerows(rows)
    resp = make_response(buf.getvalue())
    resp.headers["Content-Type"]        = "text/csv"
    resp.headers["Content-Disposition"] = f"attachment; filename=session_{sid}_data.csv"
    return resp

def _get_weekly_sessions():
    week_ago = (datetime.now() - timedelta(days=7)).isoformat()
    rows = get_all_sessions()   # already uses WAL-mode connection
    return [s for s in rows if s.get("start_time", "") >= week_ago]

@app.route("/api/weekly")
def api_weekly():
    sessions = _get_weekly_sessions()
    valid        = [s for s in sessions if s.get("avg_score") is not None]
    weekly_avg   = round(sum(s["avg_score"] for s in valid) / len(valid), 1) if valid else 0
    return jsonify({
        "sessions":           sessions,
        "weekly_avg":         weekly_avg,
        "total_sessions":     len(sessions),
        "total_blinks":       sum(s.get("total_blinks",       0) or 0 for s in sessions),
        "total_yawns":        sum(s.get("total_yawns",        0) or 0 for s in sessions),
        "total_distractions": sum(s.get("total_distractions", 0) or 0 for s in sessions),
    })

@app.route("/api/weekly/pdf")
def api_weekly_pdf():
    sessions = _get_weekly_sessions()
    try:
        path = generate_weekly_report(sessions)
        return jsonify({"path": path, "ok": True})
    except Exception as e:
        return jsonify({"ok": False, "error": str(e)})


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

@socketio.on("pause")
def on_pause():
    global _paused
    _paused = True

@socketio.on("resume")
def on_resume():
    global _paused
    _paused = False

@socketio.on("set_alert_mode")
def on_set_alert_mode(data=None):
    global _alert_mode
    mode = (data or {}).get("mode", "voice")
    _alert_mode = mode
    _alerter.set_mode(mode)

@socketio.on("generate_report")
def on_generate_report(data=None):
    stats = data or {}
    try:
        path = generate_report(stats)
        emit("report_ready", {"path": path})
    except Exception as e:
        emit("report_ready", {"error": str(e)})


# ── Entry point ────────────────────────────────────────────────────────────────
if __name__ == "__main__":
    init_db()           # create/migrate DB before accepting any requests
    _load_face_model()
    print("\n  AttentionAI — Python Backend Server")
    print("  MediaPipe 468-point face mesh + gaze + posture + drowsiness")
    print(f"  Screenshots: {SCREENSHOT_DIR}/   Database: {DB_PATH}")
    print("  Open your browser at: http://localhost:5000\n")
    socketio.run(app, host="0.0.0.0", port=5000, debug=False)
