# Arduino Uno Obstacle-Aware Motor

An Arduino Uno reads an HC-SR04 ultrasonic sensor and controls a 9V BO DC motor with a wheel:

| Distance to obstacle | Wheel        | LCD line 2         |
|----------------------|--------------|--------------------|
| more than 20 cm      | full speed   | `Clear: Full Spd`  |
| 10 cm to 20 cm       | slows down   | `Obstacle! Slow`   |
| 10 cm or closer      | stops        | `STOP! Too Close`  |

LCD line 1 always shows the live distance (`Dist: 15.2 cm`). The LED blinks 3 times each time the wheel's speed changes.

## Components

- Arduino Uno
- HC-SR04 ultrasonic sensor
- 16x2 LCD with I2C backpack (PCF8574)
- BO DC motor + wheel
- L298N motor driver module (the Uno can't power a motor from its pins)
- 9V battery (for the motor)
- LED + 220 Ω resistor
- Jumper wires, breadboard

## Wiring

### HC-SR04
| Sensor | Uno |
|--------|-----|
| VCC    | 5V  |
| GND    | GND |
| TRIG   | D9  |
| ECHO   | D10 |

### 16x2 I2C LCD
| LCD | Uno |
|-----|-----|
| VCC | 5V  |
| GND | GND |
| SDA | A4  |
| SCL | A5  |

### L298N + motor + 9V battery
| L298N        | Connect to             |
|--------------|------------------------|
| ENA          | D5 (remove the ENA jumper cap) |
| IN1          | D7                     |
| IN2          | D8                     |
| OUT1 / OUT2  | BO motor terminals     |
| 12V (+VS)    | 9V battery +           |
| GND          | 9V battery − **and** Uno GND (common ground) |

### LED
D2 → 220 Ω resistor → LED long leg (+); LED short leg (−) → GND.

## Upload

1. In the Arduino IDE, open **Library Manager** and install **LiquidCrystal I2C** by Frank de Brabander.
2. Open `arduino_obstacle_motor.ino`, select **Arduino Uno** and the right port, then upload.
3. Open Serial Monitor at 9600 baud to see each state change logged.

## Adjusting

All settings are constants at the top of the sketch:

- `SLOW_DISTANCE_CM` / `STOP_DISTANCE_CM`: the 20 cm and 10 cm thresholds.
- `SPEED_SLOW`: PWM for the slow speed (0–255). BO motors can stall below ~100. If the wheel hums but doesn't turn when slowed, raise this value.
- `BLINK_COUNT` / `BLINK_MS`: how the LED blinks on a speed change.
- If the LCD lights up but shows nothing, change `0x27` to `0x3F` in `LiquidCrystal_I2C lcd(0x27, 16, 2);`, or turn the contrast screw on the backpack.
- If the wheel spins the wrong way, swap the two motor wires on OUT1/OUT2.

## Notes on the design

- Readings use the median of 3 pings to reject bad echoes. If no echo comes back, the path counts as clear.
- A 1 cm hysteresis keeps the motor from flickering between states when an obstacle sits right on a threshold.
- Speed-ups and slow-downs ramp smoothly. Stopping happens at once.
- The code uses `millis()` timing instead of `delay()`, so the LED blinks, the LCD updates and the motor ramps while the sensor keeps running.
- The L298N drops about 2 V, so the motor gets roughly 7 V from a 9V battery. Normal 9V PP3 batteries drain fast under motor load; a 6×AA pack lasts longer.
