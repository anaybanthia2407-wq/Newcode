# ESP32 Mobile-Controlled Car

A 2WD (or 4WD) robot car driven from your phone's browser. The ESP32 DevKit V1 creates its own Wi-Fi hotspot and serves a control page with a touch joystick, speed slider, headlights, horn and an emergency STOP. You don't need to install an app or have a router.

```
 Phone (browser)  ──Wi-Fi──►  ESP32 DevKit V1  ──PWM + direction──►  L298N  ──►  DC gear motors
  joystick page    WebSocket   (hotspot 192.168.4.1)                 motor driver
```

## How it works

- The phone connects to the `ESP32-Car` hotspot and opens `http://192.168.4.1`.
- The page sends `D,x,y` joystick commands 20 times a second over a WebSocket (port 81), so latency stays low.
- The ESP32 mixes the joystick into left and right wheel speeds ("arcade drive"), so you can steer while moving and spin in place.
- **Failsafe:** if no command arrives for 600 ms (out of range, phone locked, browser closed), the car stops.
- **Ramping:** speed changes are smoothed. This protects the plastic gearboxes and avoids current spikes that can reset the ESP32.

## Materials required

| # | Item | Qty | Notes | Approx. cost (₹) |
|---|------|-----|-------|-----------------|
| 1 | ESP32 DevKit V1 (30-pin) | 1 | The controller | 350–450 |
| 2 | L298N motor driver module | 1 | Or **TB6612FNG** (better, see upgrades) | 120–180 |
| 3 | 2WD robot chassis kit | 1 | Includes 2 × TT gear motors (3–6 V), 2 wheels, a caster wheel and screws. A 4WD kit also works. | 250–450 |
| 4 | 18650 Li-ion cells (protected) | 2 | 2 in series = 7.4 V nominal | 150–250 each |
| 5 | 2 × 18650 battery holder (series) | 1 | With leads | 40–80 |
| 6 | SPST rocker/slide switch | 1 | Main power switch | 10–20 |
| 7 | Jumper wires (M-F, M-M) | ~15 | Female-female for ESP32 to L298N | 60–100 |
| 8 | Electrolytic capacitor 470–1000 µF, ≥16 V | 1 | Across the battery input, to stop brown-out resets | 10 |
| 9 | Ceramic capacitor 0.1 µF (104) | 2–4 | Soldered across each motor's terminals to reduce noise | 5 |
| 10 | Micro-USB data cable | 1 | For programming. Some cables are charge-only and won't work. | — |
| 11 | 18650 charger | 1 | Never charge Li-ion without a proper charger | 200–400 |
| *Optional* | White LEDs + 220 Ω resistors | 2 | Headlights | 10 |
| *Optional* | Active buzzer (3.3–5 V) + BC547/2N2222 + 1 kΩ | 1 | Horn | 20 |
| *Optional* | Mini buck converter (MP1584 / LM2596) | 1 | Clean 5 V for the ESP32 (recommended) | 50–100 |

**Tools:** soldering iron and solder, small screwdriver, multimeter, double-sided tape or zip ties, wire stripper.

## Wiring

### ESP32 to L298N

| L298N pin | ESP32 pin | Function |
|-----------|-----------|----------|
| ENA | GPIO32 (D32) | Left motor speed (PWM). **Remove the ENA jumper cap.** |
| IN1 | GPIO33 (D33) | Left direction A |
| IN2 | GPIO25 (D25) | Left direction B |
| IN3 | GPIO26 (D26) | Right direction A |
| IN4 | GPIO27 (D27) | Right direction B |
| ENB | GPIO13 (D13) | Right motor speed (PWM). **Remove the ENB jumper cap.** |
| GND | GND | **Common ground is mandatory** |

All six motor pins are on the same side of the DevKit V1 header, which keeps wiring tidy. They avoid the ESP32's boot-strapping pins (0, 2, 5, 12, 15) and the input-only pins (34–39), so the motors won't twitch at power-on.

### Motors and power

```
 18650 x2 (7.4V) ─(+)─ Switch ─┬─ L298N +12V
                               │
                          [1000µF cap]
                               │
 18650 x2 (7.4V) ─(−)──────────┴─ L298N GND ──── ESP32 GND

 L298N +5V (5V-EN jumper ON) ───────────────── ESP32 VIN

 L298N OUT1/OUT2 ── Left motor      (4WD: both left motors in parallel)
 L298N OUT3/OUT4 ── Right motor     (4WD: both right motors in parallel)
```

- With the 5V-EN jumper on and a supply of 12 V or less, the L298N's onboard regulator gives 5 V. Feed that to the ESP32 **VIN** pin, not 3V3.
- **Don't plug in USB while the battery is on** unless you remove the 5V→VIN wire. Otherwise two supplies fight each other.
- If a motor spins the wrong way, swap its two wires or set `LEFT_INVERT` / `RIGHT_INVERT = true` in the code.

### Optional extras

| Part | ESP32 pin |
|------|-----------|
| Headlight LED(s) (+ 220 Ω each) | GPIO4 |
| Buzzer (via NPN transistor: GPIO23 → 1 kΩ → base, buzzer between 5 V and collector, emitter to GND) | GPIO23 |
| Status LED | GPIO2 (on-board): blinks while waiting, solid when a phone is connected |

## Software setup

1. Install the **Arduino IDE** (2.x).
2. Add the ESP32 boards URL: *File → Preferences → Additional Board Manager URLs*:
   `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
   Then *Tools → Board → Boards Manager →* install **esp32 by Espressif** (2.x or 3.x both work).
3. *Sketch → Include Library → Manage Libraries →* install **WebSockets** by **Markus Sattler**.
4. Open `esp32_mobile_car.ino` and change `AP_SSID` / `AP_PASS` if you like (password must be 8+ characters).
5. Select **Tools → Board → ESP32 Dev Module** and your COM port, then click **Upload**.
   If it hangs at `Connecting....`, hold the **BOOT** button until the upload starts.
6. Open the Serial Monitor at **115200** baud to see the IP address and connection messages.

## Driving

1. Switch the car on and wait for the blue LED to blink.
2. On your phone, join the Wi-Fi **ESP32-Car** (password `drive1234`).
   Android may say *"No internet"*: choose **Stay connected / Keep Wi-Fi**. Turning off mobile data helps if the page won't load.
3. Open **http://192.168.4.1** and turn the phone to landscape.
4. Drag the joystick to drive, use the slider to set the top speed, and use the Lights, Horn and STOP buttons.
   On a laptop, use **WASD / arrow keys**; **Space** stops the car.

## Tuning and troubleshooting

| Symptom | Fix |
|---------|-----|
| Motors hum but wheels don't turn at low speed | Raise `MIN_DUTY` (try 110–130). The L298N drops about 2 V, so use 2 × 18650 rather than 4 × AA. |
| ESP32 resets/reboots when the motors start | Brown-out: add the 1000 µF capacitor, use fresh or charged cells, or power the ESP32 from a separate buck converter. Also lower `RAMP_UP`. |
| Car doesn't drive straight | Motors are never identical. Scale one side down slightly in `setDrive()` (e.g. `r = r * 95 / 100`), or add wheel encoders (see upgrades). |
| Turning is too twitchy | Lower the speed slider, or scale `x` in `setDrive()` (e.g. `x = x * 60 / 100`). |
| Page loads but "Disconnected" | Make sure you opened `http://` (not `https://`) and that the phone is still on the car's Wi-Fi. |
| One motor doesn't move | Check the ENA/ENB jumper caps are removed and the wires go to the right GPIOs. Make sure GND is common. |
| Whining noise from motors | Normal at 1 kHz PWM on an L298N. With a TB6612FNG, set `PWM_FREQ = 20000` for silent drive. |

## Suggestions to make it better

### Easy upgrades
1. **Swap the L298N for a TB6612FNG.** It's MOSFET-based, so it loses about 0.5 V instead of about 2 V, runs cooler, is smaller, and supports silent 20 kHz PWM. Same pin logic: PWMA/PWMB replace ENA/ENB, AIN1/2 and BIN1/2 replace IN1–4, and tie **STBY** to 3.3 V.
2. **Separate 5 V buck converter** for the ESP32 (MP1584/LM2596 set to 5 V). This removes nearly all random resets.
3. **Battery voltage monitor:** a 100 kΩ / 33 kΩ divider from the battery to GPIO34. Read it with `analogReadMilliVolts()`, show it on the page, and auto-slow the car below 6.6 V to protect the cells.
4. **Bigger wheels / N20 metal-gear motors** for more speed and durability than TT motors.
5. **Hide the hotspot password** in a `secrets.h` file, and change the default before showing it off.

### Feature upgrades
6. **Obstacle avoidance:** an HC-SR04 ultrasonic sensor (use a voltage divider on ECHO, since it outputs 5 V) or a VL53L0X ToF sensor (I2C). Block forward motion when something is under about 20 cm away.
7. **FPV camera:** an **ESP32-CAM** on the car streams video to the same phone page. It's the most impressive upgrade.
8. **Bluetooth mode:** use `BluetoothSerial` (classic BT) or BLE with an app like *Dabble* / *RemoteXY* if you prefer a native app.
9. **Control over your home Wi-Fi + mDNS** (`http://esp32car.local`) for longer range through routers, or **ESP-NOW** with a second ESP32 as a physical joystick remote, which gives about 200 m+ range.
10. **OTA updates** (`ArduinoOTA` / ElegantOTA), so you can reprogram without unplugging anything.
11. **Phone tilt steering** using the browser's `DeviceOrientationEvent`.
12. **Line follower mode** with a 3–5 channel IR sensor array, switchable from the page.

### Advanced
13. **Wheel encoders + PID control** for straight driving, precise turns and distance-based moves.
14. **IMU (MPU6050)** for heading hold and crash detection.
15. **Autonomous / ROS 2 (micro-ROS)** or **path recording and replay**.
16. **Custom PCB or perfboard** in place of jumper wires, with a power LED, fuse and reverse-polarity protection.

## Safety notes

- Use **protected** 18650 cells. Never short them, and never charge them unattended or without a proper charger.
- Lift the wheels off the table the first time you power it up.
- The L298N heatsink gets hot under load. Don't stall the motors for long.
