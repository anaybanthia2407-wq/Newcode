"""
Face Enrollment Tool — AttentionAI System
=========================================
Run this BEFORE starting server.py to register student faces.
The trained model (face_model.yml) is then loaded by the server
to recognise students automatically during attention monitoring.

Usage:
    py -3.11 face_trainer.py

Requirements:
    opencv-contrib-python  (not the plain opencv-python)
    mediapipe

Install / switch:
    py -3.11 -m pip uninstall opencv-python -y
    py -3.11 -m pip install opencv-contrib-python mediapipe
"""

import cv2
import os
import json
import pickle
import numpy as np
import mediapipe as mp

from database import init_db, _connect

MODEL_FILE     = "face_model.yml"
LABELS_FILE    = "face_labels.json"
SAMPLES_NEEDED = 50          # more samples → better recognition
CONF_THRESHOLD = 80          # LBPH confidence; lower = more certain match

# ── MediaPipe face detector (no Haar cascade XML needed) ─────────────────────────
_mp_det    = mp.solutions.face_detection
_face_det  = _mp_det.FaceDetection(model_selection=0, min_detection_confidence=0.65)

# ANSI colours for terminal (Windows: enable via os.system('color') or use colorama)
GRN  = "\033[92m"
YLW  = "\033[93m"
RED  = "\033[91m"
CYN  = "\033[96m"
DIM  = "\033[2m"
RST  = "\033[0m"
BOLD = "\033[1m"


# ── Helpers ───────────────────────────────────────────────────────────────────────
def _get_face_roi(frame):
    """Return (100×100 grayscale ROI, bounding_box) or (None, None)."""
    h, w = frame.shape[:2]
    rgb  = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
    res  = _face_det.process(rgb)
    if not res.detections:
        return None, None
    bb = res.detections[0].location_data.relative_bounding_box
    x1 = max(0, int(bb.xmin * w))
    y1 = max(0, int(bb.ymin * h))
    x2 = min(w, int((bb.xmin + bb.width)  * w))
    y2 = min(h, int((bb.ymin + bb.height) * h))
    if x2 <= x1 or y2 <= y1:
        return None, None
    gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
    roi  = cv2.resize(gray[y1:y2, x1:x2], (100, 100))
    return roi, (x1, y1, x2, y2)


def _load_label_map() -> dict:
    if os.path.exists(LABELS_FILE):
        with open(LABELS_FILE) as f:
            return {int(k): v for k, v in json.load(f).items()}
    return {}


def _save_label_map(label_map: dict):
    with open(LABELS_FILE, "w") as f:
        json.dump({str(k): v for k, v in label_map.items()}, f, indent=2)


def _check_opencv_contrib():
    try:
        cv2.face.LBPHFaceRecognizer_create()
        return True
    except AttributeError:
        print(f"\n{RED}  ERROR: opencv-contrib-python is not installed.{RST}")
        print(f"  Run:")
        print(f"    {YLW}py -3.11 -m pip uninstall opencv-python -y{RST}")
        print(f"    {YLW}py -3.11 -m pip install opencv-contrib-python{RST}")
        return False


def _open_camera():
    for idx in range(3):
        cap = cv2.VideoCapture(idx)
        if cap.isOpened():
            ret, _ = cap.read()
            if ret:
                cap.set(cv2.CAP_PROP_FRAME_WIDTH,  640)
                cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 480)
                return cap
        cap.release()
    return None


# ── DB helpers ────────────────────────────────────────────────────────────────────
def _list_students_raw():
    with _connect() as conn:
        return conn.execute(
            "SELECT id, name, enrolled_at, length(face_samples) FROM students ORDER BY name"
        ).fetchall()


def _get_student_id(name: str):
    with _connect() as conn:
        row = conn.execute("SELECT id FROM students WHERE name=?", (name,)).fetchone()
        return row[0] if row else None


def _save_student_samples(name: str, samples: list) -> int:
    blob = pickle.dumps(samples)
    with _connect() as conn:
        conn.execute(
            """INSERT INTO students (name, face_samples, enrolled_at)
               VALUES (?, ?, datetime('now'))
               ON CONFLICT(name) DO UPDATE
               SET face_samples = excluded.face_samples,
                   enrolled_at  = excluded.enrolled_at""",
            (name, blob)
        )
        row = conn.execute("SELECT id FROM students WHERE name=?", (name,)).fetchone()
        return row[0]


def _add_samples_for_student(name: str, new_samples: list):
    """Merge new samples with existing ones (keeps up to 120 total)."""
    with _connect() as conn:
        row = conn.execute(
            "SELECT face_samples FROM students WHERE name=?", (name,)
        ).fetchone()
        if row and row[0]:
            existing = pickle.loads(row[0])
        else:
            existing = []
    combined = (existing + new_samples)[-120:]   # keep newest 120
    _save_student_samples(name, combined)


def _delete_student(sid: int):
    with _connect() as conn:
        conn.execute("DELETE FROM students WHERE id=?", (sid,))


# ── Enrolment ─────────────────────────────────────────────────────────────────────
def enroll_student(name: str, add_more: bool = False):
    """Capture face samples and train / update the LBPH model."""
    action = "Adding more samples for" if add_more else "Enrolling"
    print(f"\n{CYN}  {action}: {BOLD}{name}{RST}")
    print(f"  Look straight at the camera.")
    print(f"  Collecting {SAMPLES_NEEDED} samples — move your head slightly for variety.")
    print(f"  Press {YLW}Q{RST} to cancel.\n")

    cap = _open_camera()
    if cap is None:
        print(f"{RED}  Cannot open webcam.{RST}")
        return

    samples    = []
    no_face_ct = 0

    while len(samples) < SAMPLES_NEEDED:
        ok, frame = cap.read()
        if not ok:
            continue
        frame    = cv2.flip(frame, 1)
        roi, box = _get_face_roi(frame)

        if roi is not None:
            samples.append(roi)
            no_face_ct = 0
            x1, y1, x2, y2 = box
            cv2.rectangle(frame, (x1, y1), (x2, y2), (0, 210, 80), 2)
            cv2.putText(frame, f"Face detected!",
                        (x1, y1 - 8), cv2.FONT_HERSHEY_SIMPLEX, 0.55, (0, 210, 80), 2)
        else:
            no_face_ct += 1
            col = (0, 100, 230)
            msg = "No face — look at the camera" if no_face_ct < 30 else "Improve lighting / move closer"
            cv2.putText(frame, msg, (10, 90), cv2.FONT_HERSHEY_SIMPLEX, 0.55, col, 1)

        pct = int(len(samples) / SAMPLES_NEEDED * 100)
        filled = pct // 5
        bar    = "█" * filled + "░" * (20 - filled)
        cv2.putText(frame, f"[{bar}] {pct}%  ({len(samples)}/{SAMPLES_NEEDED})",
                    (10, 30), cv2.FONT_HERSHEY_SIMPLEX, 0.6, (0, 210, 80), 2)
        cv2.putText(frame, f"{action}: {name}",
                    (10, 58), cv2.FONT_HERSHEY_SIMPLEX, 0.55, (200, 200, 220), 1)

        cv2.imshow("Face Enrollment  —  Press Q to cancel", frame)
        if cv2.waitKey(1) & 0xFF == ord("q"):
            print(f"\n{YLW}  Cancelled.{RST}")
            cap.release()
            cv2.destroyAllWindows()
            return

    cap.release()
    cv2.destroyAllWindows()

    print(f"\n  {GRN}✓{RST} Captured {len(samples)} samples. Saving…")

    if add_more:
        _add_samples_for_student(name, samples)
        print(f"  Samples added. Retraining model…")
    else:
        sid = _save_student_samples(name, samples)
        print(f"  Saved as student ID {sid}. Training model…")

    retrain_model()


# ── Model training ─────────────────────────────────────────────────────────────────
def retrain_model():
    """Rebuild face_model.yml from all students in the database."""
    if not _check_opencv_contrib():
        return

    with _connect() as conn:
        rows = conn.execute(
            "SELECT id, name, face_samples FROM students WHERE face_samples IS NOT NULL"
        ).fetchall()

    if not rows:
        print(f"{YLW}  No students enrolled yet — nothing to train.{RST}")
        return

    all_faces, all_labels, label_map = [], [], {}
    for sid, name, blob in rows:
        faces = pickle.loads(blob) if blob else []
        for face in faces:
            all_faces.append(face)
            all_labels.append(int(sid))
        label_map[int(sid)] = name

    recogniser = cv2.face.LBPHFaceRecognizer_create()
    recogniser.train(all_faces, np.array(all_labels, dtype=np.int32))
    recogniser.save(MODEL_FILE)
    _save_label_map(label_map)

    print(f"\n  {GRN}✓ Model trained successfully!{RST}")
    print(f"  Enrolled students ({len(rows)}):")
    for sid, name in label_map.items():
        with _connect() as conn:
            row = conn.execute(
                "SELECT length(face_samples) FROM students WHERE id=?", (sid,)
            ).fetchone()
        approx = (row[0] // 40000) if row and row[0] else 0  # rough sample count
        print(f"    {CYN}[{sid}]{RST} {name}  (~{approx} samples)")
    print(f"  Saved to: {GRN}{MODEL_FILE}{RST}\n")


# ── Live recognition test ─────────────────────────────────────────────────────────
def test_recognition():
    """Open webcam and show live recognition confidence."""
    if not _check_opencv_contrib():
        return
    if not os.path.exists(MODEL_FILE):
        print(f"{RED}  No model found — enroll students first.{RST}")
        return

    recogniser = cv2.face.LBPHFaceRecognizer_create()
    recogniser.read(MODEL_FILE)
    label_map = _load_label_map()

    print(f"\n  {CYN}Live recognition test{RST} — press {YLW}Q{RST} to exit.\n")
    cap = _open_camera()
    if cap is None:
        print(f"{RED}  Cannot open webcam.{RST}")
        return

    while True:
        ok, frame = cap.read()
        if not ok:
            continue
        frame    = cv2.flip(frame, 1)
        roi, box = _get_face_roi(frame)

        if roi is not None and box is not None:
            x1, y1, x2, y2 = box
            sid, conf = recogniser.predict(roi)
            name     = label_map.get(sid, "Unknown")
            matched  = conf < CONF_THRESHOLD
            col      = (0, 210, 80) if matched else (0, 100, 230)
            label    = f"{name}  ({conf:.0f})" if matched else f"? Unknown  ({conf:.0f})"
            cv2.rectangle(frame, (x1, y1), (x2, y2), col, 2)
            cv2.putText(frame, label, (x1, y1 - 8),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.65, col, 2)
            status = f"{'MATCHED' if matched else 'NO MATCH'}  conf={conf:.0f}  (threshold={CONF_THRESHOLD})"
        else:
            status = "No face detected"

        cv2.putText(frame, status, (10, 30), cv2.FONT_HERSHEY_SIMPLEX, 0.55, (200, 200, 220), 1)
        cv2.putText(frame, "Press Q to exit", (10, frame.shape[0] - 12),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.45, (120, 120, 140), 1)
        cv2.imshow("Recognition Test", frame)
        if cv2.waitKey(1) & 0xFF == ord("q"):
            break

    cap.release()
    cv2.destroyAllWindows()


# ── Menu helpers ──────────────────────────────────────────────────────────────────
def _show_header():
    students = _list_students_raw()
    model_ok = os.path.exists(MODEL_FILE)
    print(f"\n{BOLD}{'═'*48}{RST}")
    print(f"{BOLD}   Face Enrollment — AttentionAI System{RST}")
    print(f"{'═'*48}")
    if students:
        names = ", ".join(row[1] for row in students)
        print(f"  Enrolled students : {CYN}{len(students)}{RST}  → {GRN}{names}{RST}")
    else:
        print(f"  Enrolled students : {CYN}0{RST}")
    print(f"  Trained model     : {GRN+'Ready  ('+MODEL_FILE+')' if model_ok else RED+'Not trained yet'}{RST}")
    print(f"{'─'*48}\n")


def _show_students():
    rows = _list_students_raw()
    if not rows:
        print(f"  {YLW}No students enrolled yet.{RST}")
        return
    print(f"\n  {'ID':<5} {'Name':<25} {'Enrolled'}")
    print("  " + "─" * 52)
    for sid, name, dt, blob_sz in rows:
        approx = (blob_sz // 10200) if blob_sz else 0
        print(f"  {CYN}{sid:<5}{RST} {name:<25} {DIM}{dt or '—'}{RST}  (~{approx} samples)")
    print()


# ── Main menu ─────────────────────────────────────────────────────────────────────
def main():
    init_db()

    while True:
        _show_header()
        print(f"  {BOLD}1.{RST} Enroll a new student")
        print(f"  {BOLD}2.{RST} Add more samples for existing student")
        print(f"  {BOLD}3.{RST} List enrolled students")
        print(f"  {BOLD}4.{RST} Delete a student")
        print(f"  {BOLD}5.{RST} Retrain model (after manual DB changes)")
        print(f"  {BOLD}6.{RST} Test live recognition")
        print(f"  {BOLD}Q.{RST} Quit\n")

        choice = input("  Choose option: ").strip().upper()

        if choice == "1":
            name = input("\n  Student name: ").strip()
            if not name:
                print(f"{RED}  Name cannot be empty.{RST}")
            elif _get_student_id(name) is not None:
                overwrite = input(f"  '{name}' already enrolled. Re-enroll? (y/N): ").strip().lower()
                if overwrite == "y":
                    enroll_student(name, add_more=False)
            else:
                enroll_student(name, add_more=False)

        elif choice == "2":
            _show_students()
            rows = _list_students_raw()
            if not rows:
                continue
            name = input("  Student name to update: ").strip()
            if not name:
                continue
            if _get_student_id(name) is None:
                print(f"{RED}  '{name}' not found. Enroll them first.{RST}")
            else:
                enroll_student(name, add_more=True)

        elif choice == "3":
            _show_students()
            input("  Press Enter to continue…")

        elif choice == "4":
            _show_students()
            rows = _list_students_raw()
            if not rows:
                continue
            try:
                sid = int(input("  Enter student ID to delete: ").strip())
            except ValueError:
                continue
            match = next((r for r in rows if r[0] == sid), None)
            if not match:
                print(f"{RED}  ID {sid} not found.{RST}")
                continue
            confirm = input(f"  Delete '{match[1]}'? This also removes their face data. (y/N): ").strip().lower()
            if confirm == "y":
                _delete_student(sid)
                print(f"  {GRN}Deleted student {sid} — {match[1]}.{RST}")
                print("  Retraining model without this student…")
                retrain_model()

        elif choice == "5":
            retrain_model()
            input("  Press Enter to continue…")

        elif choice == "6":
            test_recognition()

        elif choice == "Q":
            print(f"\n  Bye!\n")
            break
        else:
            print(f"  {YLW}Invalid choice — try 1/2/3/4/5/6/Q{RST}")


if __name__ == "__main__":
    main()
