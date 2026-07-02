"""
Face Enrollment Tool — AI Attention Detection System
=====================================================
Run this ONCE per student to register their face.

Usage:
    python face_trainer.py

The script will:
  1. Ask for the student's name
  2. Open the webcam and capture 40 face samples
  3. Train an LBPH recogniser and save it as face_model.yml
  4. Store the student in the SQLite database

To recognise students automatically, server.py loads face_model.yml at startup.
"""

import cv2
import os
import sqlite3
import pickle
import json
import numpy as np
from database import init_db, DB_PATH


FACE_CASCADE = cv2.CascadeClassifier(
    cv2.data.haarcascades + "haarcascade_frontalface_default.xml"
)
MODEL_FILE  = "face_model.yml"
LABELS_FILE = "face_labels.json"
SAMPLES_NEEDED = 40


def _load_label_map() -> dict:
    if os.path.exists(LABELS_FILE):
        with open(LABELS_FILE) as f:
            return {int(k): v for k, v in json.load(f).items()}
    return {}


def _save_label_map(label_map: dict):
    with open(LABELS_FILE, "w") as f:
        json.dump({str(k): v for k, v in label_map.items()}, f, indent=2)


def enroll_student(name: str):
    """Capture face samples from webcam and train/update the recogniser."""
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
        frame = cv2.flip(frame, 1)
        gray  = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
        faces = FACE_CASCADE.detectMultiScale(gray, scaleFactor=1.3, minNeighbors=5)

        for (x, y, w, h) in faces:
            face_roi = gray[y:y+h, x:x+w]
            face_roi = cv2.resize(face_roi, (100, 100))
            samples.append(face_roi)
            cv2.rectangle(frame, (x, y), (x+w, y+h), (0, 210, 80), 2)

        pct = int(len(samples) / SAMPLES_NEEDED * 100)
        bar = "█" * (pct // 5) + "░" * (20 - pct // 5)
        cv2.putText(frame, f"[{bar}] {pct}%  ({len(samples)}/{SAMPLES_NEEDED})",
                    (10, 30), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 210, 80), 2)
        cv2.putText(frame, f"Enrolling: {name}",
                    (10, 60), cv2.FONT_HERSHEY_SIMPLEX, 0.65, (200, 200, 220), 1)
        cv2.imshow("Face Enrollment  —  Press Q to cancel", frame)

        if cv2.waitKey(1) & 0xFF == ord("q"):
            print("  Cancelled.")
            cap.release()
            cv2.destroyAllWindows()
            return

    cap.release()
    cv2.destroyAllWindows()

    if not samples:
        print("  No face detected. Try better lighting and face the camera directly.")
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

    # Retrain recogniser on ALL enrolled students
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
        samples = pickle.loads(blob)
        for s in samples:
            all_faces.append(s)
            all_labels.append(sid)
        label_map[sid] = name

    try:
        recogniser = cv2.face.LBPHFaceRecognizer_create()
    except AttributeError:
        print("\n  NOTE: opencv-contrib-python required for face recognition.")
        print("  Run:  pip uninstall opencv-python -y && pip install opencv-contrib-python")
        print("  Face model NOT saved — re-run this script after installing contrib.\n")
        return

    recogniser.train(all_faces, np.array(all_labels, dtype=np.int32))
    recogniser.save(MODEL_FILE)
    _save_label_map(label_map)

    print(f"  Face model trained: {len(rows)} student(s) — saved to {MODEL_FILE}")
    for sid, name in label_map.items():
        print(f"    [{sid}] {name}")


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
    print("  3. Retrain model (use if you added students manually)")
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
