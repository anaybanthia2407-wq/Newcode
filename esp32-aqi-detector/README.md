# ESP32 AQI Detector

An air-quality monitor for the ESP32 DevKit V1: an MQ135 gas sensor feeds
an analog reading that's converted to an approximate CO2-equivalent PPM
value, classified into a quality category, and shown live on a 128x64
I2C SSD1306 OLED.

> **Not an official AQI.** The MQ135 is a broad-spectrum gas sensor
> (CO2, NH3, benzene, alcohol, smoke...), not a particulate (PM2.5/PM10)
> sensor. The EPA's Air Quality Index is computed from PM2.5, PM10, O3,
> CO, SO2 and NO2 readings. What this project reports is a CO2-equivalent
> PPM estimate and a simplified GOOD/MODERATE/POOR/UNHEALTHY/SEVERE scale
> derived from it — useful as a relative indoor air-quality indicator,
> not a regulatory measurement.

## Hardware

- ESP32 DevKit V1
- MQ135 gas sensor module (analog output)
- 0.96" SSD1306 128x64 I2C OLED display

### Wiring

| MQ135 Pin | ESP32 Pin |
|-----------|-----------|
| VCC       | 3V3       |
| GND       | GND       |
| AOUT      | GPIO34    |

| OLED Pin | ESP32 Pin |
|----------|-----------|
| VCC      | 3V3       |
| GND      | GND       |
| SDA      | GPIO21    |
| SCL      | GPIO22    |

GPIO34 is an ADC1, input-only pin — a safe default choice for an analog
sensor on the ESP32.

**About powering the MQ135 at 3.3V vs 5V:** most MQ135 breakout modules
will run on 3.3V, which is the simplest and safest option since the
sensor's analog output then can't exceed the ESP32 ADC's 3.3V limit. The
tradeoff is somewhat reduced sensitivity compared to the sensor's rated
5V operation. If you want to run it at 5V instead, do **not** wire AOUT
straight into GPIO34 — add a resistor divider (e.g. 10kΩ from AOUT to
GPIO34, and 20kΩ from GPIO34 to GND) to bring the signal down into the
ESP32's safe range, then in the sketch set `SENSOR_VCC = 5.0` and
`DIVIDER_RATIO` to `(R1 + R2) / R2` (30/20 = 1.5 for the values above).

## Arduino IDE Setup

1. Install the ESP32 board package (File > Preferences > Additional Board Manager URLs):
   `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
2. Install libraries via Library Manager:
   - `Adafruit GFX Library`
   - `Adafruit SSD1306`
3. Select board **ESP32 Dev Module** and the correct COM port.

## Configuration

Edit these values at the top of `esp32_aqi_detector.ino` before uploading:

- `SENSOR_VCC` / `DIVIDER_RATIO` — set based on how you power the MQ135 (see wiring notes above).
- `SCREEN_ADDRESS` — I2C address of your OLED (usually `0x3C`, sometimes `0x3D`).
- `WARMUP_MS` — preheat time before the first reading. 20s is a practical minimum for a demo; the datasheet recommends a 24-48h burn-in for best long-term accuracy.
- Category thresholds in `classifyAQI()` — tune to taste; the defaults use common indoor CO2-equivalent bands (good < 700 ppm, moderate < 1000 ppm, poor < 2000 ppm, unhealthy < 5000 ppm, severe beyond that).

## Behavior

On boot, the ESP32 shows a warm-up countdown while the MQ135 heats up,
then samples the sensor for a few seconds to establish a clean-air
baseline (`RZERO`) — keep it in fresh air during this step. After that,
it continuously reads the sensor, smooths the value with an exponential
moving average to reduce jitter, and displays the PPM estimate, its
quality category, and a fill bar on the OLED once per second. Readings
are also logged to Serial at 115200 baud.

## Calibration notes

- The sensor is assumed to be in clean outdoor-reference air (~397 ppm CO2) during the boot-time calibration window. Calibrating indoors in a stuffy room will skew all subsequent readings low relative to the true value.
- `RZERO` is recalculated on every boot and not persisted; if you need a stable long-term baseline, calibrate once in known-good air, note the printed `rZero` value (add a `Serial.println(rZero);` after `calibrate()` if you want to check it), and hardcode it instead of recalibrating each run.
