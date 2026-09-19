# ESP32 Wi-Fi + Telegram Diagnostic

A standalone troubleshooting sketch for the [esp32-fire-sensor](../esp32-fire-sensor) project's two most common failure points: a Wi-Fi network that won't connect, and a Telegram alert that never arrives. Run this on its own (no flame sensor, OLED, or buzzer wiring required) to isolate whether the problem is your network, your bot credentials, or something else.

## What it checks

1. **Wi-Fi band / visibility** — scans nearby networks and prints each one's SSID, signal strength, and channel, then reports whether your configured `WIFI_SSID` actually showed up.

   Important: the ESP32 DevKit V1's built-in radio is **2.4GHz-only** (802.11 b/g/n) hardware — it cannot join, or even see, a 5GHz network. So this scan can only ever list 2.4GHz networks, and a successful connection is 2.4GHz by definition; there's no 5GHz mode to detect. If your target SSID doesn't show up in the scan, the most common causes are:
   - Your router uses "band steering" — merging 2.4GHz and 5GHz under one SSID — and its 2.4GHz radio is disabled, hidden, or not being offered to new clients.
   - The ESP32 is out of range of the 2.4GHz signal (2.4GHz has better range than 5GHz, so this is less common, but still possible).
   - A typo in `WIFI_SSID`.

   If your router supports it, try creating a separate SSID dedicated to the 2.4GHz band (most routers with band-steering have an option to split them) and point `WIFI_SSID` at that instead.

2. **Telegram delivery** — sends one test message through your bot and prints Telegram's raw HTTP response to Serial, so a failure is immediately diagnosable instead of just an opaque status code:
   - `HTTP 404` — wrong or still-placeholder `TELEGRAM_BOT_TOKEN`.
   - `HTTP 400` — wrong or still-placeholder `TELEGRAM_CHAT_ID`.
   - `HTTP 403` — you haven't sent the bot a message yet (or you blocked/deleted the chat).

## Usage

1. Fill in `WIFI_SSID`, `WIFI_PASSWORD`, `TELEGRAM_BOT_TOKEN`, and `TELEGRAM_CHAT_ID` at the top of `esp32_wifi_telegram_diagnostic.ino` — same values as in `esp32_fire_sensor.ino`.
2. Upload to the ESP32 DevKit V1 (board **ESP32 Dev Module**).
3. Open Serial Monitor at **115200 baud**.
4. Read the output: the network scan, the connection attempt result (with IP/signal/channel on success), and the Telegram test result.

The whole check re-runs automatically every 60 seconds, so you can leave Serial Monitor open while you change router settings or re-check your bot token, without needing to reset the board each time.

No extra libraries are required beyond what ships with the ESP32 board package (`WiFi`, `WiFiClientSecure`, `HTTPClient`).
