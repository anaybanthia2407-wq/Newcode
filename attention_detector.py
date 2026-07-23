"""
Core AI attention detection using MediaPipe facial landmarks.
Tracks blinks, yawns, head pose, gaze direction, drowsiness, and posture.
"""

import time
import math
import numpy as np
import cv2
import mediapipe as mp

# ── Landmark indices ─────────────────────────────────────────────────────────────
LEFT_EYE  = [362, 385, 387, 263, 373, 380]
RIGHT_EYE = [33,  160, 158, 133, 153, 144]

# Mouth landmarks for accurate MAR
MOUTH_TOP   = 13   # upper lip centre
MOUTH_BOT   = 14   # lower lip centre
MOUTH_LEFT  = 61   # left corner
MOUTH_RIGHT = 291  # right corner
MOUTH_TL    = 81   # top-left inner
MOUTH_BL    = 178  # bottom-left inner
MOUTH_TR    = 311  # top-right inner
MOUTH_BR    = 402  # bottom-right inner

NOSE_TIP     = 1
CHIN         = 152
FOREHEAD_TOP = 10

# Iris centres — require refine_landmarks=True
LEFT_IRIS_CTR  = 468
RIGHT_IRIS_CTR = 473

# Eye boundary landmarks for gaze
L_EYE_LEFT  = 263;  L_EYE_RIGHT = 362
L_EYE_TOP   = 386;  L_EYE_BOT   = 374
R_EYE_LEFT  = 33;   R_EYE_RIGHT = 133
R_EYE_TOP   = 159;  R_EYE_BOT   = 145

# ── Detection thresholds ─────────────────────────────────────────────────────────
EAR_BLINK_THRESH    = 0.21   # below = eye closed
MAR_YAWN_THRESH     = 0.48   # above = yawning
BLINK_CONSEC_FRAMES = 3      # min frames below threshold to count blink
HEAD_YAW_THRESH     = 22     # degrees head turn = distraction
DROWSY_EYE_SECS     = 4.0    # seconds of eye closure = drowsy
GAZE_H_THRESH       = 0.38   # iris offset / eye-width for LEFT/RIGHT gaze
GAZE_V_DOWN_THRESH  = 0.32   # downward iris offset (phone/desk)
POSTURE_Y_THRESH    = 0.75   # face centre Y below this = slouching
POSTURE_SIZE_THRESH = 0.10   # face height fraction below this = too far away
TREND_WINDOW        = 90     # frames used for trend regression (~6 s at 15 fps)

# ── Score EMA parameters ─────────────────────────────────────────────────────────
EMA_ALPHA_DOWN = 0.18        # how fast score drops (penalty hits)
EMA_ALPHA_UP   = 0.10        # how fast score recovers (smoother recovery)
NO_FACE_DECAY  = 1.0         # score drop per frame when no face detected


# ── Geometry helpers ─────────────────────────────────────────────────────────────
def _pt(lm, idx, w, h):
    return (lm[idx].x * w, lm[idx].y * h)


def _dist(a, b):
    dx, dy = a[0] - b[0], a[1] - b[1]
    return math.sqrt(dx * dx + dy * dy)


def eye_aspect_ratio(landmarks, eye_indices, w, h):
    pts = [_pt(landmarks, i, w, h) for i in eye_indices]
    A = _dist(pts[1], pts[5])
    B = _dist(pts[2], pts[4])
    C = _dist(pts[0], pts[3])
    return (A + B) / (2.0 * C) if C > 0 else 0.0


def mouth_aspect_ratio(landmarks, w, h):
    """Three-pair vertical / horizontal MAR for robust yawn detection."""
    v1    = _dist(_pt(landmarks, MOUTH_TL,  w, h), _pt(landmarks, MOUTH_BL,  w, h))
    v2    = _dist(_pt(landmarks, MOUTH_TOP, w, h), _pt(landmarks, MOUTH_BOT, w, h))
    v3    = _dist(_pt(landmarks, MOUTH_TR,  w, h), _pt(landmarks, MOUTH_BR,  w, h))
    horiz = _dist(_pt(landmarks, MOUTH_LEFT, w, h), _pt(landmarks, MOUTH_RIGHT, w, h))
    return (v1 + v2 + v3) / (3.0 * horiz) if horiz > 0 else 0.0


# ── Main detector ────────────────────────────────────────────────────────────────
class AttentionDetector:
    def __init__(self):
        self.mp_face_mesh = mp.solutions.face_mesh
        self.face_mesh    = self.mp_face_mesh.FaceMesh(
            max_num_faces          = 1,
            refine_landmarks       = True,
            min_detection_confidence = 0.5,
            min_tracking_confidence  = 0.5,
        )

        # Event counters
        self.blink_count        = 0
        self.yawn_count         = 0
        self.distraction_events = 0
        self.drowsy_events      = 0
        self.posture_events     = 0

        # State flags
        self._blink_frames        = 0
        self._yawn_active         = False
        self._distracted_active   = False
        self._drowsy_active       = False
        self._bad_posture_active  = False
        self._eye_close_start     = None
        self._low_attention_start = None

        # EMA-smoothed score
        self.current_score = 100.0
        self.face_detected = False

        # Latest biometric readings (read by server.py)
        self.last_ear        = 0.0
        self.last_mar        = 0.0
        self.last_yaw        = 0.0
        self.last_pitch      = 0.0
        self.last_gaze_x     = 0.0
        self.last_gaze_y     = 0.0
        self.gaze_direction  = "CENTER"
        self.last_posture    = 1.0
        self.attention_trend = 0.0
        self.is_drowsy       = False

        # Session history
        self.score_history     = []
        self.timestamp_history = []
        self.session_start     = time.time()

    # ── Main entry point ─────────────────────────────────────────────────────────
    def process_frame(self, frame):
        """Returns (annotated_frame, score, face_detected)."""
        h, w   = frame.shape[:2]
        rgb    = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
        rgb.flags.writeable = False
        results = self.face_mesh.process(rgb)
        rgb.flags.writeable = True

        annotated = frame.copy()
        now       = time.time()

        if not results.multi_face_landmarks:
            self.face_detected    = False
            self.is_drowsy        = False
            self.gaze_direction   = "NO FACE"
            self._eye_close_start = None
            self.current_score    = max(0.0, self.current_score - NO_FACE_DECAY)
            self._append_history(now)
            return annotated, self.current_score, False

        self.face_detected = True
        lm = results.multi_face_landmarks[0].landmark

        # ── Biometrics ───────────────────────────────────────────────────────
        ear = (eye_aspect_ratio(lm, LEFT_EYE,  w, h) +
               eye_aspect_ratio(lm, RIGHT_EYE, w, h)) / 2.0
        mar = mouth_aspect_ratio(lm, w, h)

        self.last_ear   = ear
        self.last_mar   = mar
        self.last_yaw   = (lm[NOSE_TIP].x - 0.5) * 180   # ±90° range
        self.last_pitch = (lm[NOSE_TIP].y - 0.5) * 90

        self._update_gaze(lm)
        self._update_posture(lm)

        # ── Blink & drowsiness ────────────────────────────────────────────────
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
                self.drowsy_events += 1
                self._drowsy_active = True
            self.is_drowsy   = True
            drowsy_penalty   = 28.0
        else:
            self._drowsy_active = False
            self.is_drowsy      = False

        # ── Yawn ──────────────────────────────────────────────────────────────
        yawn_penalty = 0.0
        if mar > MAR_YAWN_THRESH:
            if not self._yawn_active:
                self.yawn_count  += 1
                self._yawn_active = True
            yawn_penalty = 10.0
        else:
            self._yawn_active = False

        # ── Head-turn distraction ─────────────────────────────────────────────
        dist_penalty = 0.0
        if abs(self.last_yaw) > HEAD_YAW_THRESH:
            if not self._distracted_active:
                self.distraction_events += 1
                self._distracted_active  = True
            dist_penalty = 20.0
        else:
            self._distracted_active = False

        # ── Gaze penalty (LEFT / RIGHT / DOWN only — UP is ok) ────────────────
        gaze_penalty = 7.0 if self.gaze_direction in ("LEFT", "RIGHT", "DOWN") else 0.0

        # ── Posture penalty ───────────────────────────────────────────────────
        posture_penalty = 5.0 if self.last_posture < 0.5 else 0.0

        # ── EMA-smoothed score ─────────────────────────────────────────────────
        # Max total penalty = 28+20+10+7+5 = 70 → worst case score = 30
        total_penalty = drowsy_penalty + yawn_penalty + dist_penalty + gaze_penalty + posture_penalty
        frame_score   = max(0.0, 100.0 - total_penalty)

        # Drop faster than recovery for natural responsiveness
        alpha = EMA_ALPHA_DOWN if frame_score < self.current_score else EMA_ALPHA_UP
        self.current_score = (1.0 - alpha) * self.current_score + alpha * frame_score
        self.current_score = max(0.0, min(100.0, self.current_score))

        self._update_trend()
        self._append_history(now)
        self._draw_landmarks(annotated, results, w, h)
        return annotated, self.current_score, True

    # ── Gaze tracking ─────────────────────────────────────────────────────────────
    def _update_gaze(self, lm):
        try:
            # Left eye iris relative to eye centre
            lc_x = (lm[L_EYE_LEFT].x + lm[L_EYE_RIGHT].x) / 2
            lc_y = (lm[L_EYE_TOP].y  + lm[L_EYE_BOT].y)   / 2
            le_w = abs(lm[L_EYE_LEFT].x - lm[L_EYE_RIGHT].x)
            le_h = abs(lm[L_EYE_TOP].y  - lm[L_EYE_BOT].y)
            lg_x = (lm[LEFT_IRIS_CTR].x - lc_x) / max(le_w, 0.001)
            lg_y = (lm[LEFT_IRIS_CTR].y - lc_y) / max(le_h, 0.001)

            # Right eye iris relative to eye centre
            rc_x = (lm[R_EYE_LEFT].x + lm[R_EYE_RIGHT].x) / 2
            rc_y = (lm[R_EYE_TOP].y  + lm[R_EYE_BOT].y)   / 2
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
            elif self.last_gaze_y < -GAZE_V_DOWN_THRESH * 1.2:
                self.gaze_direction = "UP"
            else:
                self.gaze_direction = "CENTER"
        except Exception:
            self.gaze_direction = "CENTER"

    # ── Posture proxy ─────────────────────────────────────────────────────────────
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

    # ── Attention trend via linear regression ──────────────────────────────────────
    def _update_trend(self):
        if len(self.score_history) >= TREND_WINDOW:
            recent = self.score_history[-TREND_WINDOW:]
            x = np.arange(len(recent), dtype=float)
            try:
                slope = np.polyfit(x, recent, 1)[0]
                self.attention_trend = float(slope * TREND_WINDOW)
            except Exception:
                self.attention_trend = 0.0

    # ── Low-attention duration ────────────────────────────────────────────────────
    def get_low_attention_duration(self):
        if self.current_score < 50:
            if self._low_attention_start is None:
                self._low_attention_start = time.time()
            return time.time() - self._low_attention_start
        self._low_attention_start = None
        return 0.0

    # ── Session statistics ────────────────────────────────────────────────────────
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

    # ── Helpers ────────────────────────────────────────────────────────────────────
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
        lm = results.multi_face_landmarks[0].landmark
        try:
            ok_gaze = self.gaze_direction == "CENTER"
            col = (0, 255, 200) if ok_gaze else (0, 80, 255)
            for idx in (LEFT_IRIS_CTR, RIGHT_IRIS_CTR):
                cx, cy = int(lm[idx].x * w), int(lm[idx].y * h)
                cv2.circle(frame, (cx, cy), 3, col, -1)
            # Draw attention score on frame
            score_col = (0, 200, 100) if self.current_score >= 70 \
                   else (0, 180, 240) if self.current_score >= 40 \
                   else (0, 60, 230)
            cv2.putText(frame, f"Attn: {self.current_score:.0f}%",
                        (8, 22), cv2.FONT_HERSHEY_SIMPLEX, 0.6, score_col, 2)
            if self.is_drowsy:
                cv2.putText(frame, "DROWSY!", (8, 48),
                            cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 60, 230), 2)
        except Exception:
            pass

    def release(self):
        self.face_mesh.close()
