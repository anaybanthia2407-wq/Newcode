"""
Face Enrollment Tool — AI Attention Detection System
=====================================================
Run this ONCE per student to register their face.

Uses MediaPipe Face Detection (no Haar cascade files needed).

Usage:
    py -3.11 face_trainer.py
"""

import cv2
import os
import sqlite3
import pickle
import json
import numpy as np
import mediapipe as mp
from database import init_db, DB_PATH


MODEL_FILE     = "face_model.yml"
LABELS_FILE    = "face_labels.json"
SAMPLES_NEEDED = 40

# Use MediaPipe face detection — no external XML files needed
mp_face_det = mp.solutions.face_detection
face_detector = mp_face_det.FaceDetection(
    model_selection=0, min_detection_confidence=0.6
)


def _get_face_roi(frame):
    """
    Detect face using MediaPipe and return cropped 100x100 grayscale face ROI.
    Returns None if no face found.
    """
    h, w = frame.shape[:2]
    rgb     = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
    results = face_detector.process(rgb)

    if not results.detections:
        return None, None

    det  = results.detections[0]
    bbox = det.location_data.relative_bounding_box

    x1 = max(0, int(bbox.xmin * w))
    y1 = max(0, int(bbox.ymin * h))
    x2 = min(w, int((bbox.xmin + bbox.width)  * w))
    y2 = min(h, int((bbox.ymin + bbox.height) * h))

    if x2 <= x1 or y2 <= y1:
        return None, None

    gray    = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
    face_roi = cv2.resize(gray[y1:y2, x1:x2], (100, 100))
    return face_roi, (x1, y1, x2, y2)


def _load_label_map() -> dict:
    if os.path.exists(LABELS_FILE):
        with open(LABELS_FILE) as f:
            return {int(k): v for k, v in json.load(f).items()}
    return {}


def _save_label_map(label_map: dict):
    with open(LABELS_FILE, "w") as f:
        json.dump({str(k): v for k, v in label_map.items()}, f, indent=2)


def enroll_student(name: str):
    """Capture face samples via MediaPipe and train/update the LBPH recogniser."""
    print(f"\n  Enrolling: {name}")
    print(f"  Look at the camera. Collecting {SAMPLES_NEEDED} samples...")
    print("  Press Q to cancel.\n")

    cap = cv2.VideoCapture(0)
    cap.set(cv2.CAP_PROP_FRAME_WIDTH, 640)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 480)

    samples = []

    while len(samples) < SAMPLES_NEEDED:
        ok, frame = cap.read()
        if not ok:
            continue
        frame    = cv2.flip(frame, 1)
        roi, box = _get_face_roi(frame)

        if roi is not None:
            samples.append(roi)
            x1, y1, x2, y2 = box
            cv2.rectangle(frame, (x1, y1), (x2, y2), (0, 210, 80), 2)

        pct = int(len(samples) / SAMPLES_NEEDED * 100)
        bar = "#" * (pct // 5) + "-" * (20 - pct // 5)
        cv2.putText(frame, f"[{bar}] {pct}%  ({len(samples)}/{SAMPLES_NEEDED})",
                    (10, 30), cv2.FONT_HERSHEY_SIMPLEX, 0.65, (0, 210, 80), 2)
        cv2.putText(frame, f"Enrolling: {name}",
                    (10, 60), cv2.FONT_HERSHEY_SIMPLEX, 0.6, (200, 200, 220), 1)
        if roi is None:
            cv2.putText(frame, "No face detected — look at camera",
                        (10, 90), cv2.FONT_HERSHEY_SIMPLEX, 0.55, (0, 100, 230), 1)

        cv2.imshow("Face Enrollment  (Press Q to cancel)", frame)
        if cv2.waitKey(1) & 0xFF == ord("q"):
            print("  Cancelled.")
            cap.release()
            cv2.destroyAllWindows()
            return

    cap.release()
    cv2.destroyAllWindows()

    if not samples:
        print("  No face detected. Improve lighting and face the camera directly.")
        return

    # Save samples to database
    init_db()
    conn = sqlite3.connect(DB_PATH)
    c    = conn.cursor()
    c.execute(
        """INSERT INTO students (name, face_samples, enrolled_at)
           VALUES (?, ?, datetime('now'))
           ON CONFLICT(name) DO UPDATE SET face_samples=excluded.face_samples,
                                           enrolled_at=excluded.enrolled_at""",
        (name, pickle.dumps(samples))
    )
    student_id = c.execute(
        "SELECT id FROM students WHERE name=?", (name,)
    ).fetchone()[0]
    conn.commit()
    conn.close()

    print(f"  Saved {len(samples)} samples for {name} (id={student_id})")
    _retrain_all()


def _retrain_all():
    """Rebuild face_model.yml from all students in the database."""
    init_db()
    conn = sqlite3.connect(DB_PATH)
    rows = conn.execute(
        "SELECT id, name, face_samples FROM students WHERE face_samples IS NOT NULL"
    ).fetchall()
    conn.close()

    if not rows:
        print("  No students enrolled yet.")
        return

    all_faces, all_labels, label_map = [], [], {}
    for sid, name, blob in rows:
        for s in pickle.loads(blob):
            all_faces.append(s)
            all_labels.append(sid)
        label_map[sid] = name

    try:
        recogniser = cv2.face.LBPHFaceRecognizer_create()
    except AttributeError:
        print("\n  opencv-contrib-python required.")
        print("  Run: py -3.11 -m pip uninstall opencv-python -y")
        print("       py -3.11 -m pip install opencv-contrib-python")
        return

    recogniser.train(all_faces, np.array(all_labels, dtype=np.int32))
    recogniser.save(MODEL_FILE)
    _save_label_map(label_map)

    print(f"\n  Face model trained — {len(rows)} student(s) enrolled:")
    for sid, name in label_map.items():
        print(f"    [{sid}] {name}")
    print(f"  Model saved to: {MODEL_FILE}\n")


def list_students():
    init_db()
    conn = sqlite3.connect(DB_PATH)
    rows = conn.execute("SELECT id, name, enrolled_at FROM students").fetchall()
    conn.close()
    if not rows:
        print("  No students enrolled yet.")
    else:
        print(f"\n  {'ID':<5} {'Name':<25} {'Enrolled'}")
        print("  " + "-" * 50)
        for sid, name, dt in rows:
            print(f"  {sid:<5} {name:<25} {dt or '—'}")


if __name__ == "__main__":
    print("\n╔══════════════════════════════════════════╗")
    print("║   Face Enrollment — AttentionAI System   ║")
    print("╚══════════════════════════════════════════╝\n")
    print("  1. Enroll a new student")
    print("  2. List enrolled students")
    print("  3. Retrain model")
    print("  Q. Quit\n")

    choice = input("  Choose (1/2/3/Q): ").strip()

    if choice == "1":
        name = input("  Enter student name: ").strip()
        if name:
            enroll_student(name)
    elif choice == "2":
        list_students()
    elif choice == "3":
        _retrain_all()
    else:
        print("  Bye.")
