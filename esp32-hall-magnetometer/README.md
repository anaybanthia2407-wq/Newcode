# ESP32 Hall-Effect Magnetic Field Detector

Detects magnetic fields using the **built-in Hall-effect sensor** of the ESP32 DevKit V1. You don't need an external sensor.

- An **external LED on GPIO4** (and the on-board LED on GPIO2) **blinks** while a magnetic field is present. It blinks faster as the field gets stronger.
- A second external **LED on GPIO18 stays solidly ON** while a magnet is detected.
- A **128x64 SSD1306 I2C OLED** shows the magnetic flux density in its SI unit, the **tesla (T)**, in millitesla (mT).
- The **Arduino Serial Plotter** graphs the field strength, plus a `Magnet` line that goes high while a magnet is detected.
- The OLED also shows a strength bar, whether a magnet is detected, and which pole (+ / -) faces the chip.

## Hardware

- ESP32 DevKit V1 (ESP32-WROOM-32, the original ESP32. The S2/S3/C3 chips have **no** Hall sensor.)
- 0.96" SSD1306 128x64 I2C OLED display (address `0x3C`)
- 2 LEDs and two 220 Ω resistors
- A magnet to test with

### Wiring

| OLED Pin           | ESP32 Pin |
|--------------------|-----------|
| VCC                | 3V3       |
| GND                | GND       |
| SDA                | GPIO21    |
| SCL                | GPIO22    |

| LED                | ESP32 Pin |
|--------------------|-----------|
| Blinking LED long leg (+) via 220 Ω resistor | D4 (GPIO4) |
| Detect LED long leg (+) via 220 Ω resistor | D18 (GPIO18) |
| Both LEDs' short legs (−) | GND |

The board's built-in blue LED on GPIO2 blinks along with the external LED. To use a different pin for the external LED, change `EXT_LED_PIN`.

> Do **not** connect anything to **GPIO36 (VP)** or **GPIO39 (VN)**. The Hall sensor is read through these pins internally.

The Hall sensor sits **under the metal shield of the ESP32 module**. Bring the magnet to the top of the shield.

## Arduino IDE Setup

1. Add the ESP32 board URL in File > Preferences > Additional Board Manager URLs:
   `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
2. In Boards Manager, install **esp32 by Espressif, version 2.0.17**.
   `hallRead()` was removed in core 3.x. The sketch stops with a clear error if you compile it against 3.x.
3. In Library Manager, install **Adafruit SSD1306** and **Adafruit GFX Library**.
4. Select the **ESP32 Dev Module** board and your COM port, then upload.

## Configuration

These values are at the top of `esp32_hall_magnetometer.ino`:

- `SCREEN_ADDRESS`: the OLED's I2C address, usually `0x3C` and sometimes `0x3D`. Run an I2C scanner sketch if you're not sure.
- `DETECT_ON_MT` / `DETECT_OFF_MT`: the detection threshold in mT, with hysteresis so the LED doesn't flicker.
- `BLINK_FULL_SCALE_MT`: the field at which the LED blinks fastest.
- `DEFAULT_MT_PER_COUNT`: the sensitivity used until you calibrate.

## Calibration (for accurate tesla readings)

The built-in sensor gives raw, uncalibrated counts, and its sensitivity varies from chip to chip. `DEFAULT_MT_PER_COUNT` is only a rough starting value. For real SI readings, calibrate the sketch using a field you already know:

1. Open the Serial Monitor at **115200 baud**, with line ending set to *Newline*.
2. Keep magnets away and send `z` to zero the background field. This also happens automatically at every boot.
3. Hold a magnet whose field you know at the sensor. You can measure it with a gaussmeter, or use a magnet with a known datasheet value at a fixed distance.
4. Send `c <value in mT>`, for example `c 25`. The sketch computes and saves the sensitivity in flash.
5. Send `s` at any time to print the current settings.

Unit reminder: 1 T = 1000 mT = 1,000,000 uT. Earth's field is about 25–65 uT, which is below this sensor's noise floor. A small neodymium magnet produces several mT up to hundreds of mT at its surface.

## Serial Plotter

Open **Tools → Serial Plotter** at **115200 baud**. It graphs three lines:

- `Field_mT`: the field strength in mT (always positive, whichever pole faces the chip).
- `Magnet`: 0 with no magnet, and jumps to 10 while a magnet is detected.
- `Threshold`: the detection level, as a flat reference line.

## How it works

1. Each reading takes 9 blocks of 32 `hallRead()` samples, averages each block, and keeps the median block. This throws away noise spikes.
2. The zero offset measured at boot is subtracted.
3. The result is multiplied by the calibration factor to convert to mT, then smoothed with an exponential moving average. Readings under 0.5 mT are shown as 0 so the display doesn't flicker from noise.
4. When |B| goes above the detection threshold, the LED starts blinking. The blink period shrinks from 800 ms to 60 ms as the field rises toward `BLINK_FULL_SCALE_MT`.
5. The OLED updates every 250 ms, and the Serial Plotter data every 50 ms.

## Limitations

The ESP32's internal Hall sensor is coarse. It is noisy, its offset drifts with temperature, and it only responds to fields perpendicular to the chip. It works well for detecting a magnet and estimating its strength, but it is not a precision magnetometer. For lab-grade readings, use an external sensor such as an SS49E, DRV5053, or the 3-axis QMC5883L/MLX90393.
