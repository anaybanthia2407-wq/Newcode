# ESP32 Smart Street Light

An automatic street light for the ESP32 DevKit V1. An LDR tells day from night, and an IR obstacle sensor detects a car or object passing underneath.

| Light (LDR) | Object under IR sensor | LED       |
|-------------|------------------------|-----------|
| Day         | No                     | OFF       |
| Day         | Yes                    | **OFF**   |
| Night       | No                     | OFF       |
| Night       | Yes                    | **ON**    |

At night the LED turns on when an object arrives and turns off once it has passed. A 16x2 I2C LCD shows the mode, the light level, whether an object is detected, and the LED state.

## Components

- ESP32 DevKit V1
- IR obstacle avoidance sensor module (3 pins: VCC, GND, OUT)
- LDR (photoresistor) + 10 kΩ resistor
- LED (any colour) + 220 Ω resistor
- Display, either one (detected automatically):
  - 0.96" SSD1306 128x64 I2C OLED (address 0x3C), or
  - 16x2 LCD with I2C backpack (PCF8574, address 0x27 / 0x3F)
- Breadboard and jumper wires

## Wiring

### IR sensor
| IR Pin | ESP32 Pin |
|--------|-----------|
| VCC    | 3V3       |
| GND    | GND       |
| OUT    | GPIO35    |

### LDR (voltage divider)
```
3V3 ──── LDR ────┬──── GPIO32
                 │
               10kΩ
                 │
GND ─────────────┘
```
More light gives a higher reading (0–4095).

> Using an LDR **module** (VCC/GND/AO/DO) instead? Connect AO to GPIO32 and set `LDR_INVERTED = true`, since most modules output a lower voltage in bright light.

### LED
```
GPIO18 ──── 220Ω ──── LED (+, long leg)   LED (−, short leg) ──── GND
```

### I2C LCD
| LCD Pin | ESP32 Pin |
|---------|-----------|
| VCC     | VIN (5V)  |
| GND     | GND       |
| SDA     | GPIO21    |
| SCL     | GPIO22    |

The LCD needs 5V for proper contrast, so power it from VIN while the board is on USB. If the screen lights up but shows no text, turn the blue contrast potentiometer on the back of the backpack.

## Arduino IDE setup

1. Install the ESP32 board package (File > Preferences > Additional Board Manager URLs):
   `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
2. Install **Adafruit SSD1306** and **Adafruit GFX Library** from Library Manager (needed for the OLED). The 16x2 LCD needs no library: the sketch drives it directly over `Wire`, so it is not affected by conflicting LiquidCrystal_I2C versions.
3. Select board **ESP32 Dev Module** and the correct COM port, then upload `esp32_smart_street_light.ino`.

## Calibration

1. Open Serial Monitor at **115200 baud**. Every second it prints a line like
   `Light:2310  Mode:DAY    Object:NO   LED:OFF  Passed:0`
2. Note the `Light` value in normal room light, then cover the LDR with your hand to simulate night and note that value too.
3. Set `NIGHT_THRESHOLD` and `DAY_THRESHOLD` in the sketch between those two values. Keep `DAY_THRESHOLD` a few hundred above `NIGHT_THRESHOLD` so the mode doesn't flicker at dusk.
4. Adjust the potentiometer on the IR module until its onboard LED lights only when your object is in front of it, at the distance you want.

## Configuration

These values are at the top of the sketch:

- `IR_ACTIVE_LOW`: `true` for most IR modules, which output LOW when they detect something.
- `LDR_INVERTED`: set to `true` if your LDR reading goes down as the light gets brighter.
- `NIGHT_THRESHOLD` / `DAY_THRESHOLD`: the day/night switching points, with hysteresis between them.
- `OBJECT_CLEAR_MS`: how long the IR sensor must see nothing before the object counts as passed and the LED turns off (default 300 ms). This stops the light flickering in gaps along a car.
- Display: OLED or LCD is detected automatically from its I2C address and shown at the end of every Serial Monitor line (e.g. `Display:OLED 0x3C`). `OLED_HEIGHT` can be set to 32 for a 128x32 OLED.
- `IR_AUTO_DETECT`: learns the IR sensor's idle level at boot, so active-low and active-high modules both work. Keep the area in front of the sensor clear while the ESP32 starts.

## LCD display

```
NIGHT Light:842
Obj:YES LED:ON
```

## How it works

- **Day/night:** every 100 ms the sketch averages 16 LDR samples. It switches to NIGHT when the level drops below `NIGHT_THRESHOLD`, and back to DAY only once it rises above `DAY_THRESHOLD`.
- **Object detection:** the object counts as present as soon as the IR sensor triggers. It counts as passed once the sensor has been clear for `OBJECT_CLEAR_MS`, and the pass counter goes up by one.
- **LED rule:** `LED = isNight AND objectPresent`. If daylight returns while the LED is on, it turns off straight away.
- The loop never blocks after startup, so the sensors are checked continuously and the LCD refreshes every 250 ms.
