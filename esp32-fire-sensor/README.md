# ESP32 Fire Sensor

A flame/fire detector for the ESP32 DevKit V1 using an MH-Sensor-Series IR flame sensor module and a 128x64 I2C SSD1306 OLED display. The display shows live fire status plus the raw digital and analog sensor readings, an optional buzzer sounds while a flame is detected, and a Telegram bot sends you a message alert over Wi-Fi as soon as fire is detected.

## Hardware

- ESP32 DevKit V1
- MH-Sensor-Series flame sensor module (IR flame sensor with DO + AO outputs)
- 0.96" SSD1306 128x64 I2C OLED display
- Active-HIGH buzzer (optional)

### Wiring

| Flame Sensor Pin | ESP32 Pin |
|-------------------|-----------|
| VCC               | 3V3       |
| GND               | GND       |
| DO                | GPIO27    |
| AO                | GPIO34    |

| OLED Pin | ESP32 Pin |
|----------|-----------|
| VCC      | 3V3       |
| GND      | GND       |
| SDA      | GPIO21    |
| SCL      | GPIO22    |

| Buzzer Pin | ESP32 Pin |
|------------|-----------|
| +          | GPIO25    |
| -          | GND       |

## Arduino IDE Setup

1. Install the ESP32 board package (File > Preferences > Additional Board Manager URLs):
   `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
2. Install libraries via Library Manager:
   - `Adafruit GFX Library`
   - `Adafruit SSD1306`
3. Select board **ESP32 Dev Module** and the correct COM port.

## Telegram Bot Setup

1. In Telegram, message **@BotFather**, send `/newbot`, and follow the prompts. Copy the bot token it gives you (looks like `123456789:AAExampleTokenValue`).
2. Send your new bot any message (e.g. "hi") so it can see your chat.
3. In a browser, visit `https://api.telegram.org/bot<TOKEN>/getUpdates` (with your real token) and find `"chat":{"id": ...}` in the JSON response — that number is your chat ID.
4. Fill both values into `esp32_fire_sensor.ino` (see Configuration below).

## Configuration

Edit these values at the top of `esp32_fire_sensor.ino` before uploading:

- `WIFI_SSID` / `WIFI_PASSWORD` — your Wi-Fi credentials, needed for the Telegram alert.
- `TELEGRAM_BOT_TOKEN` / `TELEGRAM_CHAT_ID` — from the Telegram Bot Setup steps above.
- `TELEGRAM_RESEND_INTERVAL_MS` — how often (in milliseconds) to re-send the Telegram alert while a flame is still detected. Defaults to 60000 (1 minute).
- `WIFI_RETRY_INTERVAL_MS` — how often (in milliseconds) to retry the Wi-Fi connection while it's down, whether it never connected at boot or dropped later. Defaults to 30000 (30 seconds).
- `FLAME_DO_PIN` / `FLAME_AO_PIN` — GPIOs wired to the sensor's digital and analog outputs.
- `BUZZER_PIN` / `BUZZER_ENABLED` — buzzer GPIO and whether it should sound on detection.
- `SCREEN_ADDRESS` — I2C address of your OLED (usually `0x3C`, sometimes `0x3D`).
- `ANALOG_ALERT_THRESHOLD` — analog reading (0-4095) below which a flame is considered detected even if the digital comparator hasn't tripped. Lower this to require a closer/stronger flame; tune it to your sensor and environment.

## Behavior

On boot, the ESP32 connects to Wi-Fi (showing progress on the OLED); if the connection fails within 15 seconds it continues on with local-only alerts (buzzer + OLED) and keeps retrying the connection every `WIFI_RETRY_INTERVAL_MS` in the background — the same retry also kicks in if a working connection drops later, so a lost network doesn't disable Telegram alerts for good. Every 300ms it then reads both the digital (`DO`) and analog (`AO`) outputs of the flame sensor. `DO` is read with the ESP32's internal pull-up enabled, so a disconnected or floating sensor reads HIGH (no flame) instead of falsely triggering; it goes LOW when the module's onboard comparator detects IR in the flame wavelength range above its trimpot-set sensitivity. `AO` gives a raw 0-4095 reading that drops as the flame gets stronger/closer. A flame is reported when either the digital output trips or the analog reading drops below `ANALOG_ALERT_THRESHOLD`.

While a flame is detected: the buzzer (if enabled) sounds, the OLED shows "FIRE!" with both raw readings and a "CHECK AREA" warning, sensor state is logged over Serial at 115200 baud, and a Telegram message is sent through your bot — immediately when the flame is first detected, then re-sent every `TELEGRAM_RESEND_INTERVAL_MS` while it's still ongoing so the alert doesn't fire just once. The OLED also shows the status of the last Telegram send attempt (`sent`, `no Wi-Fi`, an HTTP error code, etc.) for quick troubleshooting.
