"""
Core AI attention detection using MediaPipe facial landmarks.
Tracks blinks, yawns, head pose, gaze direction, drowsiness, and posture.
"""

import time
import numpy as np
import cv2
import mediapipe as mp
from scipy.spatial import distance

# ── Landmark indices ────────────────────────────────────────────────────────────
LEFT_EYE      = [362, 385, 387, 263, 373, 380]
RIGHT_EYE     = [33,  160, 158, 133, 153, 144]
MOUTH_OUTER   = [61, 291, 81, 178, 311, 402, 13, 14]
NOSE_TIP      = 1
CHIN          = 152
FOREHEAD_TOP  = 10

# Iris centres (refine_landmarks=True required)
LEFT_IRIS_CTR  = 468
RIGHT_IRIS_CTR = 473

# Eye boundary landmarks for gaze ratio
L_EYE_LEFT  = 263;  L_EYE_RIGHT = 362
L_EYE_TOP   = 386;  L_EYE_BOT   = 374
R_EYE_LEFT  = 33;   R_EYE_RIGHT = 133
R_EYE_TOP   = 159;  R_EYE_BOT   = 145

# ── Thresholds ──────────────────────────────────────────────────────────────────
EAR_BLINK_THRESH    = 0.22
MAR_YAWN_THRESH     = 0.60
BLINK_CONSEC_FRAMES = 2
HEAD_YAW_THRESH     = 25       # degrees
DROWSY_EYE_SECS     = 3.0     # continuous closure → drowsy
GAZE_H_THRESH       = 0.30    # iris offset / eye-width for LEFT/RIGHT
GAZE_V_DOWN_THRESH  = 0.28    # iris offset downward → looking at desk/phone
POSTURE_Y_THRESH    = 0.72    # face centre y > this → low head position
POSTURE_SIZE_THRESH = 0.12    # face height / frame height < this → too far
TREND_WINDOW        = 120     # frames for slope calculation (~8 s at 15 fps)


def eye_aspect_ratio(landmarks, eye_indices, w, h):
    pts = [(int(landmarks[i].x * w), int(landmarks[i].y * h)) for i in eye_indices]
    A = distance.euclidean(pts[1], pts[5])
    B = distance.euclidean(pts[2], pts[4])
    C = distance.euclidean(pts[0], pts[3])
    return (A + B) / (2.0 * C) if C > 0 else 0.0


def mouth_aspect_ratio(landmarks, w, h):
    pts = [(int(landmarks[i].x * w), int(landmarks[i].y * h)) for i in MOUTH_OUTER]
    top   = distance.euclidean(pts[2], pts[3])
    bot   = distance.euclidean(pts[5], pts[6])
    left  = distance.euclidean(pts[0], pts[1])
    right = distance.euclidean(pts[4], pts[7])
    vert  = (top + bot) / 2.0
    horiz = (left + right) / 2.0
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

        # ── Event counters ──────────────────────────────────────────────────
        self.blink_count        = 0
        self.yawn_count         = 0
        self.distraction_events = 0
        self.drowsy_events      = 0
        self.posture_events     = 0

        # ── State flags ─────────────────────────────────────────────────────
        self._blink_frames        = 0
        self._yawn_active         = False
        self._distracted_active   = False
        self._drowsy_active       = False
        self._bad_posture_active  = False
        self._eye_close_start     = None
        self._low_attention_start = None

        # ── Score ───────────────────────────────────────────────────────────
        self._score_window = []
        self._WINDOW       = 30
        self.current_score = 100.0
        self.face_detected = False

        # ── Latest biometric readings (exposed for server.py) ───────────────
        self.last_ear        = 0.0
        self.last_mar        = 0.0
        self.last_yaw        = 0.0
        self.last_pitch      = 0.0
        self.last_gaze_x     = 0.0      # normalised horizontal iris offset
        self.last_gaze_y     = 0.0      # normalised vertical iris offset
        self.gaze_direction  = "CENTER" # CENTER / LEFT / RIGHT / DOWN / UP
        self.last_posture    = 1.0      # 1.0 = good, 0.0 = bad
        self.attention_trend = 0.0      # positive = improving, negative = declining
        self.is_drowsy       = False

        # ── Session history ─────────────────────────────────────────────────
        self.score_history     = []
        self.timestamp_history = []
        self.session_start     = time.time()

    # ── Main entry point ────────────────────────────────────────────────────
    def process_frame(self, frame):
        """Returns (annotated_frame, score, face_detected)."""
        h, w = frame.shape[:2]
        rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
        rgb.flags.writeable = False
        results = self.face_mesh.process(rgb)
        rgb.flags.writeable = True

        annotated = frame.copy()
        now = time.time()

        if not results.multi_face_landmarks:
            self.face_detected  = False
            self.is_drowsy      = False
            self.gaze_direction = "NO FACE"
            self._eye_close_start = None
            self.current_score  = max(0.0, self.current_score - 4.0)
            self._append_history(now)
            return annotated, self.current_score, False

        self.face_detected = True
        lm = results.multi_face_landmarks[0].landmark

        # ── Core biometrics ─────────────────────────────────────────────────
        ear = (eye_aspect_ratio(lm, LEFT_EYE, w, h) +
               eye_aspect_ratio(lm, RIGHT_EYE, w, h)) / 2.0
        mar = mouth_aspect_ratio(lm, w, h)

        nose_x = lm[NOSE_TIP].x
        nose_y = lm[NOSE_TIP].y

        self.last_ear   = ear
        self.last_mar   = mar
        self.last_yaw   = (nose_x - 0.5) * 180
        self.last_pitch = (nose_y - 0.5) * 90

        # ── Gaze & posture ───────────────────────────────────────────────────
        self._update_gaze(lm)
        self._update_posture(lm)

        # ── Blink & drowsiness ───────────────────────────────────────────────
        if ear < EAR_BLINK_THRESH:
            self._blink_frames += 1
            if self._eye_close_start is None:
                self._eye_close_start = now
        else:
            if self._blink_frames >= BLINK_CONSEC_FRAMES:
                self.blink_count += 1
            self._blink_frames    = 0
            self._eye_close_start = None

        drowsy_penalty = 0.0
        if self._eye_close_start and (now - self._eye_close_start) >= DROWSY_EYE_SECS:
            if not self._drowsy_active:
                self.drowsy_events  += 1
                self._drowsy_active  = True
            self.is_drowsy   = True
            drowsy_penalty   = 35.0
        else:
            self._drowsy_active = False
            self.is_drowsy      = False

        # ── Yawn ──────────────────────────────────────────────────────────────
        yawn_penalty = 0.0
        if mar > MAR_YAWN_THRESH:
            if not self._yawn_active:
                self.yawn_count  += 1
                self._yawn_active = True
            yawn_penalty = 15.0
        else:
            self._yawn_active = False

        # ── Head turn (distraction) ───────────────────────────────────────────
        dist_penalty = 0.0
        if abs(self.last_yaw) > HEAD_YAW_THRESH:
            if not self._distracted_active:
                self.distraction_events += 1
                self._distracted_active  = True
            dist_penalty = 20.0
        else:
            self._distracted_active = False

        # ── Gaze penalty ──────────────────────────────────────────────────────
        gaze_penalty = 10.0 if self.gaze_direction in ("LEFT", "RIGHT", "DOWN") else 0.0

        # ── Posture penalty ──────────────────────────────────────────────────
        posture_penalty = 8.0 if self.last_posture < 0.5 else 0.0

        # ── Rolling average score ─────────────────────────────────────────────
        raw_penalty = drowsy_penalty + yawn_penalty + dist_penalty + gaze_penalty + posture_penalty
        frame_score = max(0.0, 100.0 - raw_penalty)
        self._score_window.append(frame_score)
        if len(self._score_window) > self._WINDOW:
            self._score_window.pop(0)
        self.current_score = float(np.mean(self._score_window))

        self._update_trend()
        self._append_history(now)
        self._draw_landmarks(annotated, results, w, h)
        return annotated, self.current_score, True

    # ── Gaze tracking ────────────────────────────────────────────────────────
    def _update_gaze(self, lm):
        try:
            lc_x = (lm[L_EYE_LEFT].x + lm[L_EYE_RIGHT].x) / 2
            lc_y = (lm[L_EYE_TOP].y  + lm[L_EYE_BOT].y)  / 2
            le_w = abs(lm[L_EYE_LEFT].x - lm[L_EYE_RIGHT].x)
            le_h = abs(lm[L_EYE_TOP].y  - lm[L_EYE_BOT].y)
            lg_x = (lm[LEFT_IRIS_CTR].x - lc_x) / max(le_w, 0.001)
            lg_y = (lm[LEFT_IRIS_CTR].y - lc_y) / max(le_h, 0.001)

            rc_x = (lm[R_EYE_LEFT].x + lm[R_EYE_RIGHT].x) / 2
            rc_y = (lm[R_EYE_TOP].y  + lm[R_EYE_BOT].y)  / 2
            re_w = abs(lm[R_EYE_LEFT].x - lm[R_EYE_RIGHT].x)
            re_h = abs(lm[R_EYE_TOP].y  - lm[R_EYE_BOT].y)
            rg_x = (lm[RIGHT_IRIS_CTR].x - rc_x) / max(re_w, 0.001)
            rg_y = (lm[RIGHT_IRIS_CTR].y - rc_y) / max(re_h, 0.001)

            self.last_gaze_x = float((lg_x + rg_x) / 2)
            self.last_gaze_y = float((lg_y + rg_y) / 2)

            if abs(self.last_gaze_x) > GAZE_H_THRESH:
                self.gaze_direction = "RIGHT" if self.last_gaze_x > 0 else "LEFT"
            elif self.last_gaze_y > GAZE_V_DOWN_THRESH:
                self.gaze_direction = "DOWN"
            elif self.last_gaze_y < -GAZE_V_DOWN_THRESH:
                self.gaze_direction = "UP"
            else:
                self.gaze_direction = "CENTER"
        except Exception:
            self.gaze_direction = "CENTER"

    # ── Posture proxy (face position in frame) ───────────────────────────────
    def _update_posture(self, lm):
        try:
            face_center_y = (lm[FOREHEAD_TOP].y + lm[CHIN].y) / 2
            face_height   = abs(lm[CHIN].y - lm[FOREHEAD_TOP].y)
            bad = (face_center_y > POSTURE_Y_THRESH) or (face_height < POSTURE_SIZE_THRESH)
            if bad:
                self.last_posture = 0.0
                if not self._bad_posture_active:
                    self.posture_events     += 1
                    self._bad_posture_active = True
            else:
                self.last_posture        = 1.0
                self._bad_posture_active = False
        except Exception:
            self.last_posture = 1.0

    # ── Attention trend via linear regression ─────────────────────────────────
    def _update_trend(self):
        if len(self.score_history) >= TREND_WINDOW:
            recent = self.score_history[-TREND_WINDOW:]
            x = np.arange(len(recent), dtype=float)
            try:
                slope = np.polyfit(x, recent, 1)[0]
                self.attention_trend = float(slope * TREND_WINDOW)
            except Exception:
                self.attention_trend = 0.0

    # ── Low-attention duration ────────────────────────────────────────────────
    def get_low_attention_duration(self):
        if self.current_score < 50:
            if self._low_attention_start is None:
                self._low_attention_start = time.time()
            return time.time() - self._low_attention_start
        self._low_attention_start = None
        return 0.0

    # ── Session statistics ────────────────────────────────────────────────────
    def session_stats(self):
        avg      = float(np.mean(self.score_history)) if self.score_history else 0.0
        duration = time.time() - self.session_start
        return {
            "duration_seconds":   duration,
            "average_attention":  avg,
            "blink_count":        self.blink_count,
            "yawn_count":         self.yawn_count,
            "distraction_events": self.distraction_events,
            "drowsy_events":      self.drowsy_events,
            "posture_events":     self.posture_events,
            "score_history":      list(self.score_history),
            "timestamp_history":  list(self.timestamp_history),
        }

    # ── Helpers ───────────────────────────────────────────────────────────────
    def _append_history(self, now):
        self.score_history.append(self.current_score)
        self.timestamp_history.append(now - self.session_start)
        if len(self.score_history) > 9000:
            self.score_history.pop(0)
            self.timestamp_history.pop(0)

    def _draw_landmarks(self, frame, results, w, h):
        mp.solutions.drawing_utils.draw_landmarks(
            frame,
            results.multi_face_landmarks[0],
            self.mp_face_mesh.FACEMESH_CONTOURS,
            mp.solutions.drawing_utils.DrawingSpec(color=(0, 200, 80), thickness=1, circle_radius=1),
            mp.solutions.drawing_utils.DrawingSpec(color=(0, 120, 255), thickness=1),
        )
        # Draw iris dots
        lm = results.multi_face_landmarks[0].landmark
        try:
            col = (0, 255, 200) if self.gaze_direction == "CENTER" else (0, 80, 255)
            for idx in (LEFT_IRIS_CTR, RIGHT_IRIS_CTR):
                cx, cy = int(lm[idx].x * w), int(lm[idx].y * h)
                cv2.circle(frame, (cx, cy), 3, col, -1)
        except Exception:
            pass

    def release(self):
        self.face_mesh.close()
