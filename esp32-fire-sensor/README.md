# ESP32 Fire Sensor

A flame/fire detector for the ESP32 DevKit V1 using an MH-Sensor-Series IR flame sensor module and a 128x64 I2C SSD1306 OLED display. The display shows live fire status plus the raw digital and analog sensor readings, and an optional buzzer sounds while a flame is detected.

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

## Configuration

Edit these values at the top of `esp32_fire_sensor.ino` before uploading:

- `FLAME_DO_PIN` / `FLAME_AO_PIN` — GPIOs wired to the sensor's digital and analog outputs.
- `BUZZER_PIN` / `BUZZER_ENABLED` — buzzer GPIO and whether it should sound on detection.
- `SCREEN_ADDRESS` — I2C address of your OLED (usually `0x3C`, sometimes `0x3D`).
- `ANALOG_ALERT_THRESHOLD` — analog reading (0-4095) below which a flame is considered detected even if the digital comparator hasn't tripped. Lower this to require a closer/stronger flame; tune it to your sensor and environment.

## Behavior

Every 300ms the ESP32 reads both the digital (`DO`) and analog (`AO`) outputs of the flame sensor. `DO` goes LOW when the module's onboard comparator detects IR in the flame wavelength range above its trimpot-set sensitivity; `AO` gives a raw 0-4095 reading that drops as the flame gets stronger/closer. A flame is reported when either the digital output trips or the analog reading drops below `ANALOG_ALERT_THRESHOLD`. The OLED shows "FIRE!" or "No Fire" along with both raw readings, sensor state is logged over Serial at 115200 baud, and the buzzer (if enabled) sounds for the duration of the detection.
