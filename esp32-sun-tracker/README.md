# ESP32 Sun Tracker

A light-seeking tracker for the ESP32 DevKit V1. An LDR (light sensor) mounted on a servo motor finds the brightest light source, either the sun or a phone flashlight for indoor demos, and turns to face it. A 16x2 I2C LCD shows what the tracker is doing, and an LED lights up whenever the motor is turning.

## Hardware

| Part | Qty | Notes |
|------|-----|-------|
| ESP32 DevKit V1 | 1 | |
| SG90 / MG90S micro servo | 1 | |
| LDR (photoresistor, 5 mm) | 1 | |
| 10 kΩ resistor | 1 | For the LDR voltage divider |
| LED (any colour) | 1 | |
| 220 Ω resistor | 1 | LED current limit |
| 16x2 LCD with I2C backpack (PCF8574) | 1 | |
| Breadboard + jumper wires | | |
| Small black tube (straw, pen cap, rolled black paper) | 1 | Optional, but strongly recommended (see [Mechanical build](#mechanical-build)) |
| 470–1000 µF capacitor | 1 | Optional, across servo 5V/GND to stop resets |

### Wiring

**LDR (voltage divider)**

| From | To |
|------|----|
| 3V3 | LDR leg 1 |
| LDR leg 2 | GPIO34 **and** one end of the 10 kΩ resistor |
| Other end of 10 kΩ resistor | GND |

More light means a lower LDR resistance, so GPIO34 reads a higher value.

**Servo**

| Servo Wire | ESP32 Pin |
|------------|-----------|
| Orange / Yellow (signal) | GPIO13 |
| Red (+5V) | VIN |
| Brown / Black (GND) | GND |

**LED**

| From | To |
|------|----|
| GPIO26 | 220 Ω resistor → LED long leg (+) |
| LED short leg (−) | GND |

**LCD (I2C backpack)**

| LCD Pin | ESP32 Pin |
|---------|-----------|
| VCC | VIN (5V) |
| GND | GND |
| SDA | GPIO21 |
| SCL | GPIO22 |

### Circuit diagram

```
                ESP32 DevKit V1
             ┌───────────────────┐
   3V3 ──┬───┤ 3V3               │
         │   │                   │
        LDR  │                   │
         │   │                   │
         ├───┤ GPIO34      GPIO13├────────── Servo signal (orange)
         │   │                   │
       10kΩ  │             GPIO26├──220Ω──►|── GND   (LED)
         │   │                   │
   GND ──┴───┤ GND         GPIO21├────────── LCD SDA
             │             GPIO22├────────── LCD SCL
             │                VIN├──┬─────── Servo red (+5V)
             │                   │  └─────── LCD VCC
             │                GND├────────── Servo brown, LCD GND
             └───────────────────┘
```

Notes:
- **GPIO34** is an input-only ADC1 pin. ADC1 pins keep working if you add Wi-Fi later; ADC2 pins do not.
- **VIN** gives 5V only when the board is powered over USB. A single SG90 is fine from USB. For a bigger servo (MG996R and similar), use a separate 5V supply and connect its GND to the ESP32 GND.
- If the ESP32 resets or the LCD flickers when the servo moves, add the 470–1000 µF capacitor across the servo's 5V and GND.

## Mechanical build

1. Fix the servo flat on a base so its shaft points up.
2. Glue or tape the LDR onto the servo horn (the arm). It should point sideways, in the same direction as the horn.
3. **Put the LDR at the bottom of a short black tube** (about 2–3 cm, e.g. a piece of black straw). A bare LDR picks up light from almost every direction, which makes it hard to tell where the light is coming from. The tube makes it see only what is straight in front of it, so tracking is much sharper.
4. Use long, flexible wires to the LDR so they don't pull when the servo turns.
5. Optional: mount a small solar panel next to the LDR. Then the panel always faces the light too.

## How it works

```
power on
   │
   ▼
SWEEP 0°→180°, reading the LDR every 5°  (LED on)  ◄──────────────┐
   │                                                              │
   ├── all readings too dark ──► WAITING FOR LIGHT                │
   │                               light appears / every 20 s ────┤
   ▼                                                              │
TURN to the brightest angle  (LED on)                             │
   │                                                              │
   ▼                                                              │
TRACKING: every 20 s peek ±5°, move if brighter  (LED on)         │
   │                                                              │
   └── light drops below 70% of locked level ─────────────────────┘
```

1. **Sweep:** the servo sweeps across its full range and reads the LDR every 5°.
2. **Turn:** if the brightest reading is above the threshold, the servo turns to that angle. The LED is lit the whole time the motor moves.
3. **Track:** the tracker keeps watching the light. Every 20 seconds it peeks 5° left and right and moves if either side is brighter. This lets it follow the sun slowly across the sky.
4. **Re-search:** if the light drops below 70% of its locked level (flashlight moved or switched off, cloud), it sweeps again.
5. **Waiting:** if no bright light is found, it waits. It sweeps again as soon as light hits the sensor, or every 20 seconds anyway.

An LDR only measures brightness, so it cannot tell the sun from a phone flashlight. Whichever is brightest is "the sun" as far as the tracker is concerned, and that is what makes the flashlight demo work.

### LCD messages

| When | Line 1 | Line 2 |
|------|--------|--------|
| Power on | `ESP32 Sun` | `Tracker ready` |
| Sweeping | `Scanning for` | `Ang: 45 Lvl: 62%` (live) |
| Light found | `Sun found!` | `Turning to 120°` |
| Arrived | `Facing the sun` | `Locked on!` |
| Tracking | `Facing the sun` | `Ang:120 Lvl: 85%` (live) |
| Peeking left/right | `Fine tuning...` | |
| Light moved / gone | `Sun moved/lost!` | `Searching...` |
| Nothing bright found | `No sun found` | `Waiting...` |
| Waiting | `Waiting for sun` | `Dark          8%` (live) |

### LED behaviour

| Event | LED |
|-------|-----|
| Power on | Blinks 3 times (self-test) |
| Servo sweeping, turning or fine tuning | **ON** |
| Servo still | OFF |

## Arduino IDE Setup

1. Install the ESP32 board package (File > Preferences > Additional Board Manager URLs):
   `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
2. Install libraries via Library Manager:
   - `ESP32Servo` by Kevin Harrington (use version 3.x if your ESP32 board package is 3.x)
   - `LiquidCrystal I2C` by Frank de Brabander. The IDE may warn that it "may be incompatible with esp32"; it works fine.
3. Select board **ESP32 Dev Module** and the correct COM port.
4. Open `esp32_sun_tracker.ino` and upload. If the upload stalls at `Connecting...`, hold the **BOOT** button until it starts.

## Calibration

Every LDR is a bit different, so tune the thresholds once:

1. Open **Serial Monitor** at **115200 baud**. The sketch prints the raw light reading (0–4095) several times a second.
2. Note the reading in normal room light, then with the phone flashlight (or sunlight) shining into the sensor.
3. Set `LIGHT_THRESHOLD` at the top of the sketch to a value between the two. For example, if the room reads ~800 and the flashlight reads ~3500, use ~2000.

Other settings at the top of the sketch:

| Setting | Default | What it does |
|---------|---------|--------------|
| `LCD_ADDRESS` | `0x27` | Change to `0x3F` if the LCD backlight is on but no text appears |
| `LIGHT_THRESHOLD` | `1500` | Minimum reading that counts as "sun found" |
| `LOST_RATIO` | `0.70` | Re-sweep when light falls below this fraction of the locked level |
| `SCAN_STEP` | `5` | Degrees between readings while sweeping (smaller is more precise but slower) |
| `MOVE_DELAY_MS` | `15` | Turning speed: milliseconds per degree |
| `NUDGE_MS` | `20000` | How often to fine-tune while tracking. Use ~60000 for real sun tracking |
| `DARK_RESCAN_MS` | `20000` | How often to re-sweep while no light is found |

## Testing it with a phone flashlight

1. Power it on in a dim room. It sweeps once and shows `No sun found` / `Waiting...`.
2. Turn on your phone flashlight and hold it ~30 cm away, off to one side.
3. Within 20 seconds (or immediately if the light reaches the sensor) it sweeps, shows `Sun found!` / `Turning to XX°`, lights the LED and turns toward the flashlight.
4. Slowly move the flashlight sideways. The tracker either fine-tunes toward it or, if the light drops sharply, shows `Sun moved/lost!` and sweeps again.
5. Switch the flashlight off. It sweeps, finds nothing and goes back to waiting.

## Troubleshooting

| Problem | Fix |
|---------|-----|
| LCD backlight on, no text | Turn the blue contrast screw on the back of the LCD. Then try `LCD_ADDRESS = 0x3F`. |
| LCD blank, no backlight | Check VCC is on VIN (5V), not 3V3, and that SDA/SCL aren't swapped. |
| Reading is always ~0 or ~4095 | The LDR divider is miswired. The LDR goes to 3V3, the 10 kΩ to GND, and GPIO34 to the point between them. |
| Brighter light gives a *lower* reading | The LDR and the 10 kΩ resistor are swapped. Swap them back. |
| Servo jitters or the ESP32 resets | Add the capacitor across the servo power, or power the servo from a separate 5V supply with a shared GND. |
| Points roughly at the light but not exactly | Add the black tube around the LDR and/or lower `SCAN_STEP`. |
| Keeps re-sweeping | Lower `LOST_RATIO` (e.g. `0.5`), or raise `LIGHT_THRESHOLD` above the room-light level. |
