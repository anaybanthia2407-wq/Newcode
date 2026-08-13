# ESP32 OLED Clock

A Wi-Fi-synced digital clock for the ESP32 DevKit V1 with a 128x64 I2C SSD1306 OLED display. Time is fetched via NTP on boot and kept locally by the ESP32's RTC, since the board has no battery-backed real-time clock of its own.

## Hardware

- ESP32 DevKit V1
- 0.96" SSD1306 128x64 I2C OLED display

### Wiring

| OLED Pin | ESP32 Pin |
|----------|-----------|
| VCC      | 3V3       |
| GND      | GND       |
| SDA      | GPIO21    |
| SCL      | GPIO22    |

## Arduino IDE Setup

1. Install the ESP32 board package (File > Preferences > Additional Board Manager URLs):
   `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
2. Install libraries via Library Manager:
   - `Adafruit GFX Library`
   - `Adafruit SSD1306`
3. Select board **ESP32 Dev Module** and the correct COM port.

## Configuration

Edit these values at the top of `esp32_oled_clock.ino` before uploading:

- `WIFI_SSID` / `WIFI_PASSWORD` — your Wi-Fi credentials.
- `GMT_OFFSET_SEC` / `DAYLIGHT_OFFSET_SEC` — your timezone offset from UTC in seconds. Defaults to IST (UTC+5:30).
- `SCREEN_ADDRESS` — I2C address of your OLED (usually `0x3C`, sometimes `0x3D`).

## Behavior

On boot, the ESP32 connects to Wi-Fi, syncs the current time from an NTP server, then displays a live `HH:MM:SS` clock with the date underneath, updating every 200ms. If the time sync is ever lost it shows a status message instead of a stale clock.
