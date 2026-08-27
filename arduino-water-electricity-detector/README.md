# Arduino Water / Electrical Leak Detector

Detects water (or an electrically live/leaking water path) using two bare
jumper wires as sensing probes, then warns with an OLED message, a blinking
LED, and a buzzer.

## How it works

Water — even tap water — conducts electricity weakly due to dissolved
minerals. Two probes are wired so that when both are touching (or bridged
by) water, current flows between them and pulls down the voltage read on
an analog pin (held high by the Arduino's internal pull-up when dry). The
sketch polls that pin continuously:

- **Dry / no contact**: internal pull-up holds the reading high (near
  1023), screen shows `SAFE`.
- **Water bridges the probes**: reading drops below `WATER_THRESHOLD`, and
  the board immediately switches to alert mode.

Because the probe pair behaves the same way whether the water is merely wet
or is carrying a fault current from a nearby live wire, this circuit doubles
as a simple "electrified water" warning — any conductive bridge between the
two probes trips it.

## Hardware

- Arduino Uno
- 0.96" SSD1306 128x64 I2C OLED display
- LED + ~220 ohm resistor
- Active buzzer
- 2 jumper wires (bare ends) as water-sensing probes

No external pull resistor is needed for the water probes — the sketch
enables the Uno's internal pull-up resistor on `A0` in code instead.

### Wiring

| Component            | Arduino Pin              |
|-----------------------|--------------------------|
| Probe A (drive)       | GND                      |
| Probe B (sense)       | A0                       |
| OLED VCC              | 5V                       |
| OLED GND              | GND                      |
| OLED SDA              | A4                       |
| OLED SCL              | A5                       |
| LED anode (+)         | D8 (through 220 ohm resistor) |
| LED cathode (-)       | GND                      |
| Buzzer +               | D7                       |
| Buzzer -               | GND                      |

Strip about 1cm of insulation off two jumper wires for the probes and space
their bare tips roughly 1cm apart, both positioned in/near the water to be
monitored. Probe A stays at a constant GND; Probe B is held HIGH on `A0` by
the Uno's internal pull-up (enabled via `pinMode(A0, INPUT_PULLUP)`) so it
reads near 1023 when dry, and drops when water bridges the two probes to
GND.

## Arduino IDE Setup

1. Select board **Arduino Uno** and the correct COM port.
2. Install libraries via Library Manager:
   - `Adafruit GFX Library`
   - `Adafruit SSD1306`
3. Upload `water_electricity_detector.ino`.

## Configuration

Edit these values at the top of the sketch if needed:

- `WATER_THRESHOLD` — analog reading (0-1023) below which water is
  considered detected. Lower it if the sensor false-triggers on humidity or
  electrical noise; raise it if it misses light water contact.
- `BLINK_INTERVAL_MS` / `BUZZ_INTERVAL_MS` — how fast the LED blinks and the
  buzzer pulses while an alert is active.
- `SCREEN_ADDRESS` — I2C address of your OLED (usually `0x3C`, sometimes
  `0x3D`).

## Behavior

On boot, the OLED shows an initializing message, then the board continuously
samples the water probes. While dry, the screen shows `SAFE` with the live
sensor reading. As soon as water bridges the probes, the screen switches to
a `DANGER!` alert, the LED blinks, and the buzzer pulses on and off — all
three continue until the probes are dry again.

## Notes

- If you're using a **passive** buzzer instead of an active one, replace the
  `digitalWrite(BUZZER_PIN, ...)` calls with `tone(BUZZER_PIN, 1000)` /
  `noTone(BUZZER_PIN)` to produce an audible pitch.
- This circuit senses conductivity, not voltage directly — it will not
  distinguish "wet from tap water" from "wet from a live wire fault". Treat
  any alert as a signal to cut power and investigate, not as a certified
  electrical safety device.
