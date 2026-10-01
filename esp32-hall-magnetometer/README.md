# ESP32 Hall-Effect Magnetic Field Detector

Detects magnetic fields using the **built-in Hall-effect sensor** of the ESP32 DevKit V1. You don't need an external sensor.

- An **external LED on GPIO4** (and the on-board LED on GPIO2) **blinks** while a magnetic field is present. It blinks faster as the field gets stronger.
- A **16x2 I2C LCD** shows the magnetic flux density in its SI unit, the **tesla (T)**. Readings of 1 mT or more show as **millitesla (mT)**, and weaker readings show as **microtesla (uT)**.
- The LCD's second line shows whether a magnet is detected and which pole (+ / -) faces the chip.

## Hardware

- ESP32 DevKit V1 (ESP32-WROOM-32, the original ESP32. The S2/S3/C3 chips have **no** Hall sensor.)
- 16x2 character LCD with a PCF8574 I2C backpack
- 1 LED and a 220 Ω resistor
- A magnet to test with

### Wiring

| LCD (I2C backpack) | ESP32 Pin |
|--------------------|-----------|
| VCC                | VIN (5V)  |
| GND                | GND       |
| SDA                | GPIO21    |
| SCL                | GPIO22    |

| LED                | ESP32 Pin |
|--------------------|-----------|
| Long leg (+, anode) via 220 Ω resistor | D4 (GPIO4) |
| Short leg (−, cathode) | GND |

The board's built-in blue LED on GPIO2 blinks along with the external LED. To use a different pin for the external LED, change `EXT_LED_PIN`.

> Do **not** connect anything to **GPIO36 (VP)** or **GPIO39 (VN)**. The Hall sensor is read through these pins internally.

The Hall sensor sits **under the metal shield of the ESP32 module**. Bring the magnet to the top of the shield.

## Arduino IDE Setup

1. Add the ESP32 board URL in File > Preferences > Additional Board Manager URLs:
   `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
2. In Boards Manager, install **esp32 by Espressif, version 2.0.17**.
   `hallRead()` was removed in core 3.x. The sketch stops with a clear error if you compile it against 3.x.
3. In Library Manager, install **LiquidCrystal I2C** by Frank de Brabander.
4. Select the **ESP32 Dev Module** board and your COM port, then upload.

## Configuration

These values are at the top of `esp32_hall_magnetometer.ino`:

- `LCD_ADDR`: the I2C address of the LCD, usually `0x27` and sometimes `0x3F`.
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

## How it works

1. Each reading averages 64 `hallRead()` samples to reduce noise.
2. The zero offset measured at boot is subtracted.
3. The result is multiplied by the calibration factor to convert to mT, then smoothed with an exponential moving average.
4. When |B| goes above the detection threshold, the LED starts blinking. The blink period shrinks from 800 ms to 60 ms as the field rises toward `BLINK_FULL_SCALE_MT`.
5. The LCD and Serial output update every 250 ms.

## Limitations

The ESP32's internal Hall sensor is coarse. It is noisy, its offset drifts with temperature, and it only responds to fields perpendicular to the chip. It works well for detecting a magnet and estimating its strength, but it is not a precision magnetometer. For lab-grade readings, use an external sensor such as an SS49E, DRV5053, or the 3-axis QMC5883L/MLX90393.
