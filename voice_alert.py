"""
Non-blocking voice alert system using pyttsx3 in a background thread.
"""

import threading
import time

try:
    import pyttsx3
    _TTS_AVAILABLE = True
except ImportError:
    _TTS_AVAILABLE = False


class VoiceAlert:
    def __init__(self):
        self._lock = threading.Lock()
        self._speaking = False
        self._cooldown_until = 0.0
        self.COOLDOWN = 15.0  # seconds between alerts

    def speak(self, message: str):
        """Fire-and-forget: speak in a background thread if not on cooldown."""
        now = time.time()
        with self._lock:
            if self._speaking or now < self._cooldown_until:
                return
            self._speaking = True
            self._cooldown_until = now + self.COOLDOWN

        t = threading.Thread(target=self._do_speak, args=(message,), daemon=True)
        t.start()

    def _do_speak(self, message: str):
        try:
            if _TTS_AVAILABLE:
                engine = pyttsx3.init()
                engine.setProperty("rate", 160)
                engine.setProperty("volume", 1.0)
                engine.say(message)
                engine.runAndWait()
                engine.stop()
        except Exception:
            pass  # silently ignore TTS errors
        finally:
            with self._lock:
                self._speaking = False
