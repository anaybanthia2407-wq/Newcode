"""
SQLite database manager for the Attention Detection System.
Stores students, sessions, every movement frame, and low-attention screenshots.

WAL journal mode is enabled so the detection thread and Flask API thread can
access the database simultaneously without "database is locked" errors.
"""

import sqlite3
import os
from datetime import datetime
from contextlib import contextmanager

DB_PATH = "attention_data.db"


# ── Connection helper ─────────────────────────────────────────────────────────────
@contextmanager
def _connect():
    """Open a WAL-mode connection, yield it, then commit and close."""
    conn = sqlite3.connect(DB_PATH, check_same_thread=False, timeout=10)
    conn.execute("PRAGMA journal_mode=WAL")   # concurrent readers + one writer
    conn.execute("PRAGMA synchronous=NORMAL") # safe but faster than FULL
    conn.row_factory = sqlite3.Row
    try:
        yield conn
        conn.commit()
    except Exception:
        conn.rollback()
        raise
    finally:
        conn.close()


# ── Schema creation + migration ───────────────────────────────────────────────────
def init_db():
    """Create all tables (if missing) and migrate older schemas."""
    with _connect() as conn:
        conn.executescript("""
            CREATE TABLE IF NOT EXISTS students (
                id           INTEGER PRIMARY KEY AUTOINCREMENT,
                name         TEXT NOT NULL UNIQUE,
                face_samples BLOB,
                enrolled_at  TEXT
            );

            CREATE TABLE IF NOT EXISTS sessions (
                id                 INTEGER PRIMARY KEY AUTOINCREMENT,
                student_name       TEXT,
                start_time         TEXT,
                end_time           TEXT,
                avg_score          REAL,
                total_blinks       INTEGER DEFAULT 0,
                total_yawns        INTEGER DEFAULT 0,
                total_distractions INTEGER DEFAULT 0,
                total_drowsy       INTEGER DEFAULT 0,
                total_posture      INTEGER DEFAULT 0
            );

            CREATE TABLE IF NOT EXISTS movements (
                id         INTEGER PRIMARY KEY AUTOINCREMENT,
                session_id INTEGER,
                timestamp  REAL,
                score      REAL,
                ear        REAL,
                mar        REAL,
                yaw        REAL,
                pitch      REAL,
                gaze_dir   TEXT,
                posture    REAL,
                event      TEXT,
                FOREIGN KEY (session_id) REFERENCES sessions(id)
            );

            CREATE TABLE IF NOT EXISTS screenshots (
                id         INTEGER PRIMARY KEY AUTOINCREMENT,
                session_id INTEGER,
                taken_at   TEXT,
                score      REAL,
                filepath   TEXT,
                FOREIGN KEY (session_id) REFERENCES sessions(id)
            );
        """)

        # ── Schema migration: add columns introduced in later versions ──────────
        _add_column_if_missing(conn, "sessions",  "total_drowsy",  "INTEGER DEFAULT 0")
        _add_column_if_missing(conn, "sessions",  "total_posture", "INTEGER DEFAULT 0")
        _add_column_if_missing(conn, "movements", "gaze_dir",      "TEXT")
        _add_column_if_missing(conn, "movements", "posture",       "REAL")


def _add_column_if_missing(conn, table, column, col_def):
    """ALTER TABLE only if the column doesn't already exist."""
    existing = {row[1] for row in conn.execute(f"PRAGMA table_info({table})")}
    if column not in existing:
        conn.execute(f"ALTER TABLE {table} ADD COLUMN {column} {col_def}")


# ── Session management ─────────────────────────────────────────────────────────────
class SessionDB:
    """Manages one study session in the database."""

    def __init__(self, student_name: str = "Student"):
        init_db()
        self.student_name = student_name
        self.session_id   = None
        self._buf         = []   # movement buffer; flushed every 30 frames
        self._start_session()

    def _start_session(self):
        with _connect() as conn:
            cur = conn.execute(
                "INSERT INTO sessions (student_name, start_time) VALUES (?, ?)",
                (self.student_name, datetime.now().isoformat())
            )
            self.session_id = cur.lastrowid

    def log_movement(self, timestamp: float, score: float,
                     ear: float, mar: float, yaw: float, pitch: float,
                     gaze_dir: str = None, posture: float = None,
                     event: str = None):
        """Buffer one frame; flush to DB every 30 frames."""
        self._buf.append((
            self.session_id, timestamp, score,
            ear, mar, yaw, pitch, gaze_dir, posture, event
        ))
        if len(self._buf) >= 30:
            self._flush()

    def _flush(self):
        if not self._buf:
            return
        try:
            with _connect() as conn:
                conn.executemany(
                    """INSERT INTO movements
                       (session_id, timestamp, score, ear, mar, yaw, pitch,
                        gaze_dir, posture, event)
                       VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)""",
                    self._buf
                )
            self._buf.clear()
        except Exception as e:
            print(f"  DB flush error: {e}")

    def log_screenshot(self, score: float, filepath: str):
        try:
            with _connect() as conn:
                conn.execute(
                    """INSERT INTO screenshots (session_id, taken_at, score, filepath)
                       VALUES (?, ?, ?, ?)""",
                    (self.session_id, datetime.now().isoformat(), score, filepath)
                )
        except Exception as e:
            print(f"  DB screenshot error: {e}")

    def end_session(self, avg_score: float, blinks: int,
                    yawns: int, distractions: int,
                    drowsy: int = 0, posture_events: int = 0):
        """Flush remaining buffer and finalise the session record."""
        self._flush()
        try:
            with _connect() as conn:
                conn.execute(
                    """UPDATE sessions
                       SET end_time=?, avg_score=?,
                           total_blinks=?, total_yawns=?,
                           total_distractions=?, total_drowsy=?, total_posture=?
                       WHERE id=?""",
                    (datetime.now().isoformat(), avg_score,
                     blinks, yawns, distractions, drowsy, posture_events,
                     self.session_id)
                )
        except Exception as e:
            print(f"  DB end_session error: {e}")


# ── Query helpers ──────────────────────────────────────────────────────────────────
def get_all_sessions():
    """Return all sessions as a list of dicts, newest first."""
    init_db()
    with _connect() as conn:
        rows = conn.execute(
            "SELECT * FROM sessions ORDER BY start_time DESC"
        ).fetchall()
    return [dict(r) for r in rows]


def get_session_movements(session_id: int):
    """Return all movement rows for a session, oldest first."""
    init_db()
    with _connect() as conn:
        rows = conn.execute(
            "SELECT * FROM movements WHERE session_id=? ORDER BY timestamp",
            (session_id,)
        ).fetchall()
    return [dict(r) for r in rows]


def get_session_screenshots(session_id: int):
    """Return all screenshots for a session."""
    init_db()
    with _connect() as conn:
        rows = conn.execute(
            "SELECT * FROM screenshots WHERE session_id=? ORDER BY taken_at",
            (session_id,)
        ).fetchall()
    return [dict(r) for r in rows]
