"""
Core AI attention detection using MediaPipe facial landmarks.
Tracks eye blinks, yawns, and head pose to compute an attention score (0-100).
"""

import time
import numpy as np
import cv2
import mediapipe as mp
from scipy.spatial import distance

# MediaPipe landmark indices
LEFT_EYE  = [362, 385, 387, 263, 373, 380]
RIGHT_EYE = [33,  160, 158, 133, 153, 144]
MOUTH_OUTER = [61, 291, 81, 178, 311, 402, 13, 14]
NOSE_TIP = 1
LEFT_IRIS_CENTER  = 468
RIGHT_IRIS_CENTER = 473

EAR_BLINK_THRESH  = 0.22   # Eye aspect ratio below this = eye closed
MAR_YAWN_THRESH   = 0.6    # Mouth aspect ratio above this = yawn
BLINK_CONSEC_FRAMES = 2
HEAD_YAW_THRESH   = 25     # degrees off-center = looking away


def eye_aspect_ratio(landmarks, eye_indices, img_w, img_h):
    pts = [(int(landmarks[i].x * img_w), int(landmarks[i].y * img_h))
           for i in eye_indices]
    A = distance.euclidean(pts[1], pts[5])
    B = distance.euclidean(pts[2], pts[4])
    C = distance.euclidean(pts[0], pts[3])
    return (A + B) / (2.0 * C) if C > 0 else 0.0


def mouth_aspect_ratio(landmarks, img_w, img_h):
    pts = [(int(landmarks[i].x * img_w), int(landmarks[i].y * img_h))
           for i in MOUTH_OUTER]
    top    = distance.euclidean(pts[2], pts[3])
    bottom = distance.euclidean(pts[5], pts[6])
    left   = distance.euclidean(pts[0], pts[1])
    width  = distance.euclidean(pts[4], pts[7])
    vert   = (top + bottom) / 2.0
    horiz  = (left + width) / 2.0
    return vert / horiz if horiz > 0 else 0.0


class AttentionDetector:
    def __init__(self):
        self.mp_face_mesh = mp.solutions.face_mesh
        self.face_mesh = self.mp_face_mesh.FaceMesh(
            max_num_faces=1,
            refine_landmarks=True,
            min_detection_confidence=0.5,
            min_tracking_confidence=0.5,
        )

        # Counters
        self.blink_count         = 0
        self.yawn_count          = 0
        self.distraction_events  = 0

        # State trackers
        self._blink_frames        = 0
        self._eye_was_closed      = False
        self._yawn_active         = False
        self._distracted_active   = False
        self._low_attention_start = None

        # Rolling window for smoothing (last 30 frames)
        self._score_window = []
        self._WINDOW = 30

        self.current_score = 100.0
        self.face_detected = False

        # Last biometric readings (exposed for server.py)
        self.last_ear   = 0.0
        self.last_mar   = 0.0
        self.last_yaw   = 0.0
        self.last_pitch = 0.0

        # Per-session history for report
        self.score_history     = []
        self.timestamp_history = []
        self.session_start     = time.time()

    # ------------------------------------------------------------------
    def process_frame(self, frame):
        """
        Analyse a BGR frame. Returns (annotated_frame, score, face_detected).
        score is 0-100 where 100 = fully attentive.
        """
        h, w = frame.shape[:2]
        rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
        rgb.flags.writeable = False
        results = self.face_mesh.process(rgb)
        rgb.flags.writeable = True

        annotated = frame.copy()
        now = time.time()

        if not results.multi_face_landmarks:
            self.face_detected = False
            # Rapidly decay score when no face found
            self.current_score = max(0.0, self.current_score - 4.0)
            self._append_history(now)
            return annotated, self.current_score, False

        self.face_detected = True
        lm = results.multi_face_landmarks[0].landmark

        ear_l = eye_aspect_ratio(lm, LEFT_EYE,  w, h)
        ear_r = eye_aspect_ratio(lm, RIGHT_EYE, w, h)
        ear   = (ear_l + ear_r) / 2.0
        mar   = mouth_aspect_ratio(lm, w, h)

        # --- Head yaw/pitch via nose-tip offset ---
        nose_x  = lm[NOSE_TIP].x
        nose_y  = lm[NOSE_TIP].y
        yaw_deg = abs((nose_x - 0.5) * 180)

        # Store for external access (e.g. server.py)
        self.last_ear   = ear
        self.last_mar   = mar
        self.last_yaw   = (nose_x - 0.5) * 180   # signed
        self.last_pitch = (nose_y - 0.5) * 90    # signed

        # --- Blink detection ---
        blink_penalty = 0.0
        if ear < EAR_BLINK_THRESH:
            self._blink_frames += 1
        else:
            if self._blink_frames >= BLINK_CONSEC_FRAMES:
                self.blink_count += 1
            self._blink_frames = 0

        # --- Yawn detection ---
        yawn_penalty = 0.0
        if mar > MAR_YAWN_THRESH:
            if not self._yawn_active:
                self.yawn_count += 1
                self._yawn_active = True
            yawn_penalty = 15.0
        else:
            self._yawn_active = False

        # --- Distraction (head turned) ---
        distraction_penalty = 0.0
        if yaw_deg > HEAD_YAW_THRESH:
            if not self._distracted_active:
                self.distraction_events += 1
                self._distracted_active = True
            distraction_penalty = 20.0
        else:
            self._distracted_active = False

        # --- Eyes closed long = penalty ---
        eyes_closed_penalty = 0.0
        if self._blink_frames >= BLINK_CONSEC_FRAMES * 5:
            eyes_closed_penalty = 25.0

        raw_penalty = yawn_penalty + distraction_penalty + eyes_closed_penalty

        # Smooth via rolling window
        frame_score = max(0.0, 100.0 - raw_penalty)
        self._score_window.append(frame_score)
        if len(self._score_window) > self._WINDOW:
            self._score_window.pop(0)
        self.current_score = float(np.mean(self._score_window))

        self._append_history(now)
        self._draw_landmarks(annotated, results, w, h)
        return annotated, self.current_score, True

    # ------------------------------------------------------------------
    def get_low_attention_duration(self):
        """Seconds attention has been continuously below 50."""
        if self.current_score < 50:
            if self._low_attention_start is None:
                self._low_attention_start = time.time()
            return time.time() - self._low_attention_start
        else:
            self._low_attention_start = None
            return 0.0

    # ------------------------------------------------------------------
    def session_stats(self):
        avg = float(np.mean(self.score_history)) if self.score_history else 0.0
        duration = time.time() - self.session_start
        return {
            "duration_seconds": duration,
            "average_attention": avg,
            "blink_count":       self.blink_count,
            "yawn_count":        self.yawn_count,
            "distraction_events": self.distraction_events,
            "score_history":     list(self.score_history),
            "timestamp_history": list(self.timestamp_history),
        }

    # ------------------------------------------------------------------
    def _append_history(self, now):
        self.score_history.append(self.current_score)
        self.timestamp_history.append(now - self.session_start)
        # Keep last 10 minutes at ~15 fps
        if len(self.score_history) > 9000:
            self.score_history.pop(0)
            self.timestamp_history.pop(0)

    def _draw_landmarks(self, frame, results, w, h):
        mp.solutions.drawing_utils.draw_landmarks(
            frame,
            results.multi_face_landmarks[0],
            self.mp_face_mesh.FACEMESH_CONTOURS,
            mp.solutions.drawing_utils.DrawingSpec(
                color=(0, 200, 80), thickness=1, circle_radius=1),
            mp.solutions.drawing_utils.DrawingSpec(
                color=(0, 120, 255), thickness=1),
        )

    def release(self):
        self.face_mesh.close()
