"""
SQLite database manager for the Attention Detection System.
Stores students, sessions, every movement frame, and low-attention screenshots.
"""

import sqlite3
import os
from datetime import datetime

DB_PATH = "attention_data.db"


def init_db():
    """Create all tables if they don't exist."""
    conn = sqlite3.connect(DB_PATH)
    c = conn.cursor()
    c.executescript("""
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
            total_distractions INTEGER DEFAULT 0
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
    conn.commit()
    conn.close()


class SessionDB:
    """Manages one study session in the database."""

    def __init__(self, student_name: str = "Student"):
        init_db()
        self.student_name = student_name
        self.session_id   = None
        self._conn_log    = []   # buffer movements, flush every 30 frames
        self._start_session()

    def _start_session(self):
        conn = sqlite3.connect(DB_PATH)
        c    = conn.cursor()
        c.execute(
            "INSERT INTO sessions (student_name, start_time) VALUES (?, ?)",
            (self.student_name, datetime.now().isoformat())
        )
        self.session_id = c.lastrowid
        conn.commit()
        conn.close()

    def log_movement(self, timestamp: float, score: float,
                     ear: float, mar: float, yaw: float, pitch: float,
                     event: str = None):
        """Buffer a single frame's data; flush every 30 frames for performance."""
        self._conn_log.append((self.session_id, timestamp, score,
                               ear, mar, yaw, pitch, event))
        if len(self._conn_log) >= 30:
            self._flush()

    def _flush(self):
        if not self._conn_log:
            return
        conn = sqlite3.connect(DB_PATH)
        conn.executemany(
            """INSERT INTO movements
               (session_id, timestamp, score, ear, mar, yaw, pitch, event)
               VALUES (?, ?, ?, ?, ?, ?, ?, ?)""",
            self._conn_log
        )
        conn.commit()
        conn.close()
        self._conn_log.clear()

    def log_screenshot(self, score: float, filepath: str):
        """Record a screenshot taken during low attention."""
        conn = sqlite3.connect(DB_PATH)
        conn.execute(
            """INSERT INTO screenshots (session_id, taken_at, score, filepath)
               VALUES (?, ?, ?, ?)""",
            (self.session_id, datetime.now().isoformat(), score, filepath)
        )
        conn.commit()
        conn.close()

    def end_session(self, avg_score: float, blinks: int,
                    yawns: int, distractions: int):
        """Finalise the session record."""
        self._flush()
        conn = sqlite3.connect(DB_PATH)
        conn.execute(
            """UPDATE sessions
               SET end_time=?, avg_score=?,
                   total_blinks=?, total_yawns=?, total_distractions=?
               WHERE id=?""",
            (datetime.now().isoformat(), avg_score,
             blinks, yawns, distractions, self.session_id)
        )
        conn.commit()
        conn.close()


def get_all_sessions():
    """Return a list of all sessions as dicts."""
    init_db()
    conn = sqlite3.connect(DB_PATH)
    conn.row_factory = sqlite3.Row
    rows = conn.execute(
        "SELECT * FROM sessions ORDER BY start_time DESC"
    ).fetchall()
    conn.close()
    return [dict(r) for r in rows]


def get_session_movements(session_id: int):
    """Return all movement rows for a session."""
    init_db()
    conn = sqlite3.connect(DB_PATH)
    conn.row_factory = sqlite3.Row
    rows = conn.execute(
        "SELECT * FROM movements WHERE session_id=? ORDER BY timestamp",
        (session_id,)
    ).fetchall()
    conn.close()
    return [dict(r) for r in rows]


def get_session_screenshots(session_id: int):
    """Return all screenshots for a session."""
    init_db()
    conn = sqlite3.connect(DB_PATH)
    conn.row_factory = sqlite3.Row
    rows = conn.execute(
        "SELECT * FROM screenshots WHERE session_id=? ORDER BY taken_at",
        (session_id,)
    ).fetchall()
    conn.close()
    return [dict(r) for r in rows]
