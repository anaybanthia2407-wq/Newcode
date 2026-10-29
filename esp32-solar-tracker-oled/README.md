# ESP32 Solar Tracker (OLED)

A solar tracker for the ESP32 DevKit V1. A 3-pin LDR module mounted on an SG90 servo finds the sun, or a phone flashlight for indoor demos, and turns to face it. A 0.96" OLED shows the tracker's status, the servo angle, and a dial with a needle pointing where the sensor is facing.

## Hardware

- ESP32 DevKit V1
- SG90 servo
- 3-pin LDR module (VCC, GND, DO)
- 0.96" SSD1306 128x64 I2C OLED (4 pins)

### Wiring

| Part | Pin | ESP32 Pin |
|------|-----|-----------|
| OLED | VCC | 3V3 |
| OLED | GND | GND |
| OLED | SDA | GPIO21 |
| OLED | SCL | GPIO22 |
| LDR module | VCC | **3V3** (not 5V: the ESP32 pins are 3.3V only) |
| LDR module | GND | GND |
| LDR module | DO | GPIO34 |
| SG90 servo | Orange (signal) | GPIO13 |
| SG90 servo | Red (+5V) | VIN |
| SG90 servo | Brown (GND) | GND |

Glue the LDR module to the servo arm so the sensor faces the same way as the arm. For better aim, put the LDR (the small round sensor head) inside a short black tube, like a piece of black straw.

## Arduino IDE Setup

1. Install the ESP32 board package (File > Preferences > Additional Board Manager URLs):
   `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
2. Install libraries via Library Manager:
   - `Adafruit SSD1306` (say yes when it asks to install `Adafruit GFX Library` too)
   - `ESP32Servo` by Kevin Harrington
3. Select board **ESP32 Dev Module** and the correct COM port, then upload. If the upload stalls at `Connecting...`, hold the **BOOT** button.

## Setting the LDR module (do this first)

The module has a small blue potentiometer and a DO LED. The DO LED turns on when the module sees bright light.

1. Power the board in normal room light.
2. Turn the potentiometer with a small screwdriver until the DO LED is just **OFF**.
3. Shine the phone flashlight at the sensor. The DO LED should turn **ON**.

If the DO LED is on in room light, the tracker thinks the whole room is the sun. If it doesn't turn on with the flashlight, the tracker never finds anything.

## How it works

A 3-pin module only reports BRIGHT or DARK, not how bright. So instead of looking for the brightest angle, the tracker looks for the middle of the bright area:

1. **Sweep:** the servo sweeps 0°→180° and marks every angle where the module says BRIGHT. These show up as ticks around the dial on the OLED.
2. **Turn:** it turns to the middle of the widest bright patch, which is where the light is.
3. **Track:** every 30 seconds it finds the two edges of the bright patch and moves to the middle again. This follows the sun across the sky.
4. **Search again:** if the module goes DARK for more than 0.4 seconds (light moved or switched off), it sweeps again.
5. **Wait:** if a sweep finds nothing, it waits. It sweeps again as soon as light hits the sensor, or every 20 seconds anyway.

### OLED screen

```
     SOLAR TRACKER
─────────────────────
Facing the sun              ← status
Angle:120  Sun:YES
        ' ' '
      '   ☀   '             ← ticks = where the last sweep saw light
    '      \     '            needle + sun icon = where the sensor faces
           ●
```

| Status | Meaning |
|--------|---------|
| `Scanning for sun...` | Sweeping 0°→180° |
| `Sun found! Go 120` | Turning to face the light |
| `Facing the sun` | Locked on |
| `Fine tuning...` | Checking the edges of the light and re-centring |
| `Sun moved/lost!` | Light disappeared, about to sweep again |
| `No sun found` / `Waiting for sun...` | Nothing bright found |

The dial's 0° is on the right and 180° is on the left. If your servo is mounted the other way round, the needle will look mirrored, but the tracking still works.

## Settings

At the top of `esp32_solar_tracker_oled.ino`:

| Setting | Default | What it does |
|---------|---------|--------------|
| `BRIGHT_LEVEL` | `LOW` | Most modules pull DO LOW when bright. If the OLED shows `Sun:YES` in the dark and `NO` with the flashlight, change it to `HIGH`. |
| `SCREEN_ADDRESS` | `0x3C` | Try `0x3D` if the OLED stays blank |
| `SCAN_STEP` | `3` | Degrees between checks while sweeping |
| `MOVE_DELAY_MS` | `15` | Turning speed: milliseconds per degree |
| `RECENTER_MS` | `30000` | How often to re-centre while tracking. Use ~60000 for real sunlight |
| `DARK_RESCAN_MS` | `20000` | How often to re-sweep while no sun is found |

## Troubleshooting

| Problem | Fix |
|---------|-----|
| OLED still shows an old project | The upload didn't finish. Check the IDE says `Done uploading` (hold **BOOT** while it says `Connecting...`). |
| OLED blank | Check SDA→21 and SCL→22 aren't swapped, then try `SCREEN_ADDRESS 0x3D`. |
| `Sun:YES` all the time | Turn the module's potentiometer until its DO LED is off in room light. |
| `Sun:NO` even with the flashlight | Turn the potentiometer the other way, or bring the flashlight closer. |
| `Sun:` shows the opposite of what it should | Change `BRIGHT_LEVEL` to `HIGH`. |
| Servo jitters or the ESP32 resets | Power over a good USB cable/port, or add a 470–1000 µF capacitor across servo 5V and GND. |
| Points near the light but not exactly at it | Put the sensor head in a black tube, and lower `SCAN_STEP` to `2`. |
