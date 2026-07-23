"""
Non-blocking alert system — supports voice (TTS), beep, or silent modes.
"""

import threading
import time
import math
import struct
import wave
import io
import os

try:
    import pyttsx3
    _TTS_AVAILABLE = True
except ImportError:
    _TTS_AVAILABLE = False

try:
    import winsound
    _WIN_SOUND = True
except ImportError:
    _WIN_SOUND = False


class VoiceAlert:
    def __init__(self, mode: str = "voice"):
        self._lock           = threading.Lock()
        self._speaking       = False
        self._cooldown_until = 0.0
        self.COOLDOWN        = 15.0
        self.mode            = mode   # "voice" | "beep" | "silent"

    def set_mode(self, mode: str):
        self.mode = mode

    def speak(self, message: str):
        now = time.time()
        with self._lock:
            if self._speaking or now < self._cooldown_until:
                return
            self._speaking       = True
            self._cooldown_until = now + self.COOLDOWN
        threading.Thread(target=self._do_alert, args=(message,), daemon=True).start()

    def _do_alert(self, message: str):
        try:
            if self.mode == "silent":
                pass
            elif self.mode == "beep":
                self._beep()
            else:
                if _TTS_AVAILABLE:
                    engine = pyttsx3.init()
                    engine.setProperty("rate", 160)
                    engine.setProperty("volume", 1.0)
                    engine.say(message)
                    engine.runAndWait()
                    engine.stop()
                else:
                    self._beep()
        except Exception:
            pass
        finally:
            with self._lock:
                self._speaking = False

    def _beep(self):
        if _WIN_SOUND:
            try:
                winsound.Beep(880, 250)
                time.sleep(0.08)
                winsound.Beep(660, 180)
                return
            except Exception:
                pass
        self._software_beep(880, 0.25)

    def _software_beep(self, freq: float, duration: float):
        """Generate a WAV beep and play it via OS command."""
        sample_rate = 44100
        n_samples   = int(sample_rate * duration)
        buf = io.BytesIO()
        with wave.open(buf, "w") as wf:
            wf.setnchannels(1)
            wf.setsampwidth(2)
            wf.setframerate(sample_rate)
            frames = b"".join(
                struct.pack("<h", int(
                    32767
                    * math.sin(2 * math.pi * freq * i / sample_rate)
                    * max(0.0, 1.0 - i / n_samples)
                ))
                for i in range(n_samples)
            )
            wf.writeframes(frames)
        tmp = os.path.join(os.path.dirname(__file__), "_beep.wav")
        with open(tmp, "wb") as f:
            f.write(buf.getvalue())
        try:
            if os.name == "nt":
                import subprocess
                subprocess.Popen(
                    ["powershell", "-c", f'(New-Object Media.SoundPlayer "{tmp}").PlaySync()'],
                    creationflags=subprocess.CREATE_NO_WINDOW,
                )
            else:
                os.system(f"aplay {tmp} 2>/dev/null || afplay {tmp} 2>/dev/null")
        except Exception:
            pass
