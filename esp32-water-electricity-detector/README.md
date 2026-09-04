# ESP32 Water / Electrical Leak Detector with Telegram Alerts

Detects water (or an electrically live/leaking water path) using two bare
jumper wires as sensing probes, then warns locally with an OLED message, a
blinking LED, and a buzzer — and remotely by sending a message to a
Telegram chat via a bot, using the ESP32's Wi-Fi.

This is the ESP32 DevKit V1 counterpart to the Arduino Uno version in
`../arduino-water-electricity-detector/`, with Wi-Fi/Telegram alerting
added on top.

## How it works

Water — even tap water — conducts electricity weakly due to dissolved
minerals. Two probes are wired so that when both are touching (or bridged
by) water, current flows between them and pulls down the voltage read on
an ADC pin (held high by the ESP32's internal pull-up when dry). The
sketch polls that pin continuously:

- **Dry / no contact**: internal pull-up holds the reading high (near
  4095), screen shows `SAFE`.
- **Water bridges the probes**: reading drops below `WATER_THRESHOLD`, and
  the board switches to alert mode — OLED warning, blinking LED, buzzer,
  and a Telegram message.

Because the probe pair behaves the same way whether the water is merely wet
or is carrying a fault current from a nearby live wire, this circuit doubles
as a simple "electrified water" warning — any conductive bridge between the
two probes trips it.

## Hardware

- ESP32 DevKit V1
- 0.96" SSD1306 128x64 I2C OLED display
- LED + ~220 ohm resistor
- Active buzzer
- 2 jumper wires (bare ends) as water-sensing probes

No external pull resistor is needed for the water probes — the sketch
enables the ESP32's internal pull-up in code instead.

### Wiring

| Component        | ESP32 Pin                     |
|-------------------|-------------------------------|
| Probe A (drive)   | GND                            |
| Probe B (sense)   | GPIO32                         |
| OLED VCC          | 5V (see note below)             |
| OLED GND          | GND                             |
| OLED SDA          | GPIO21                          |
| OLED SCL          | GPIO22                          |
| LED anode (+)     | GPIO25 (through 220 ohm resistor) |
| LED cathode (-)   | GND                             |
| Buzzer +           | GPIO26                          |
| Buzzer -           | GND                             |

Strip about 1cm of insulation off two jumper wires for the probes and space
their bare tips roughly 1cm apart, both positioned in/near the water to be
monitored. Probe A stays at a constant GND; Probe B is held HIGH on GPIO32
by the ESP32's internal pull-up (enabled via `pinMode(32, INPUT_PULLUP)`)
so it reads near 4095 when dry, and drops when water bridges the two probes
to GND.

**Why GPIO32 and not GPIO34-39:** the ESP32's ADC1 pins (GPIO32-39) can all
do `analogRead()`, but GPIO34-39 are input-only and have no internal pull
resistors — they'd need an external one. GPIO32 and GPIO33 are regular
GPIOs with pull-up/pull-down support, so they're the ones that work with
this resistor-free wiring.

**Why OLED VCC goes to 5V instead of 3V3:** some SSD1306 modules will
respond over I2C (and even report a successful `display.begin()`) on 3V3
but never actually light the panel — their onboard charge pump doesn't get
enough headroom at 3.3V. If your screen stays dark despite Serial showing
a successful init, try powering it from 5V instead; SDA/SCL stay 3.3V
logic either way, since those lines aren't affected by the VCC change.

## Arduino IDE Setup

1. Install the ESP32 board package (File > Preferences > Additional Board
   Manager URLs):
   `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
2. Install libraries via Library Manager:
   - `Adafruit GFX Library`
   - `Adafruit SSD1306`
   (`WiFi`, `WiFiClientSecure`, and `HTTPClient` ship with the ESP32 board
   package — no separate install needed.)
3. Select board **ESP32 Dev Module** and the correct COM port.
4. Upload `water_electricity_detector.ino`.

## Telegram Bot Setup

1. In Telegram, message **@BotFather**, send `/newbot`, and follow the
   prompts to name your bot. You'll get a **bot token** that looks like
   `123456789:AAExampleTokenTextHere`.
2. Send your new bot any message (e.g. "hi") so it registers your chat.
3. In a browser, visit:
   `https://api.telegram.org/bot<YOUR_TOKEN>/getUpdates`
   and find `"chat":{"id": ...}` in the JSON response — that number is your
   **chat ID**.
4. Fill in `TELEGRAM_BOT_TOKEN` and `TELEGRAM_CHAT_ID` near the top of the
   sketch with these values.

## Configuration

Edit these values at the top of the sketch:

- `WIFI_SSID` / `WIFI_PASSWORD` — your Wi-Fi credentials.
- `TELEGRAM_BOT_TOKEN` / `TELEGRAM_CHAT_ID` — from the Telegram bot setup
  above.
- `WATER_THRESHOLD` — ADC reading (0-4095) below which water is considered
  detected. Lower it if the sensor false-triggers on humidity or
  electrical noise; raise it if it misses light water contact.
- `BLINK_INTERVAL_MS` / `BUZZ_INTERVAL_MS` — how fast the LED blinks and the
  buzzer pulses while an alert is active.
- `ALERT_REPEAT_INTERVAL_MS` — how often (in ms) to re-send the Telegram
  alert while water is still detected, so you're not flooded with messages
  every loop iteration. Default is 60000 (1 minute).
- `WIFI_CONNECT_TIMEOUT_MS` — how long to try connecting to Wi-Fi on boot
  before giving up and continuing offline. Default is 15000 (15 seconds).
- `SCREEN_ADDRESS` — I2C address of your OLED (usually `0x3C`, sometimes
  `0x3D`).

## Behavior

On boot, the OLED shows an initializing message, then tries to connect to
Wi-Fi for up to `WIFI_CONNECT_TIMEOUT_MS` (showing progress dots). If it
connects, the OLED shows the assigned IP; if it times out, the OLED shows
"Wi-Fi connect failed / Running offline" and the sketch continues without
Wi-Fi — local detection, the OLED, LED, and buzzer all still work, but
Telegram alerts are skipped until Wi-Fi is available. After that, the
board continuously samples the water probes:

- **Dry**: screen shows `SAFE` with the live sensor reading; LED, buzzer,
  and Telegram stay silent.
- **Water detected**: screen switches to a `DANGER!` alert, the LED
  blinks, the buzzer pulses on and off, and a Telegram message is sent
  immediately. If the water stays detected, a reminder message repeats
  every `ALERT_REPEAT_INTERVAL_MS`.
- **Back to dry**: LED and buzzer stop, and a one-time "alert cleared"
  message is sent to Telegram.

## Notes

- `client.setInsecure()` skips TLS certificate validation for the Telegram
  HTTPS request. This is the common approach in ESP32 Telegram-bot
  tutorials to avoid pinning Telegram's certificate (which can expire/
  rotate), but it does mean the connection isn't verifying it's really
  talking to Telegram. For a hobby/home alerting project this is a normal
  tradeoff; if you need stricter security, pin Telegram's current root CA
  certificate with `client.setCACert()` instead.
- If you're using a **passive** buzzer instead of an active one, replace the
  `digitalWrite(BUZZER_PIN, ...)` calls with `tone(BUZZER_PIN, 1000)` /
  `noTone(BUZZER_PIN)` to produce an audible pitch.
- This circuit senses conductivity, not voltage directly — it will not
  distinguish "wet from tap water" from "wet from a live wire fault". Treat
  any alert as a signal to cut power and investigate, not as a certified
  electrical safety device.
- Never commit your real `TELEGRAM_BOT_TOKEN` or Wi-Fi password to a public
  repository — treat the token like a password, since anyone who has it can
  send messages as your bot.
