/*
 * Obstacle-Aware Motor Controller - Arduino Uno + L298N + OLED display
 *
 * Parts: Arduino Uno, HC-SR04 ultrasonic sensor, 0.96" 128x64 I2C OLED (SSD1306),
 *        L298N motor driver, one BO DC motor + wheel, LED + 220 ohm resistor
 *
 *   distance > 20 cm        -> wheel runs at FULL speed
 *   10 cm < distance <= 20  -> wheel SLOWS DOWN
 *   distance <= 10 cm       -> wheel STOPS
 *   OLED shows distance, status and a speed bar. LED blinks whenever the speed changes.
 *
 * Power (one 9-12V DC adapter, 1A or more, runs everything - no battery):
 *   Adapter     -> Arduino DC barrel jack
 *   Arduino VIN -> L298N 12V terminal
 *   Arduino GND -> L298N GND terminal
 *   L298N 5V terminal: leave unconnected
 *
 * Libraries needed (Arduino IDE > Library Manager):
 *   "Adafruit SSD1306" and "Adafruit GFX Library" (say yes to installing dependencies)
 */

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ---------------- Pin map ----------------
const uint8_t TRIG_PIN  = 9;   // HC-SR04 TRIG
const uint8_t ECHO_PIN  = 10;  // HC-SR04 ECHO
const uint8_t LED_PIN   = 2;   // LED (through 220 ohm resistor)

// L298N - motor on channel A:  ENA -> D5, IN1 -> D7, IN2 -> D8, motor on OUT1/OUT2
//         motor on channel B:  ENB -> D5, IN3 -> D7, IN4 -> D8, motor on OUT3/OUT4
// Remove the jumper cap from ENA (or ENB) so D5 can control the speed.
const uint8_t EN_PIN    = 5;   // ENA or ENB (PWM speed)
const uint8_t DIR1_PIN  = 7;   // IN1 or IN3
const uint8_t DIR2_PIN  = 8;   // IN2 or IN4
// OLED I2C: SDA -> A4, SCL -> A5

// ---------------- OLED ----------------
const uint8_t SCREEN_W  = 128;
const uint8_t SCREEN_H  = 64;
const uint8_t OLED_ADDR = 0x3C;   // some modules use 0x3D
Adafruit_SSD1306 display(SCREEN_W, SCREEN_H, &Wire, -1);
bool oledOk = false;

// ---------------- Settings ----------------
const float SLOW_DISTANCE_CM = 20.0;
const float STOP_DISTANCE_CM = 10.0;
const float HYSTERESIS_CM    = 1.0;   // stops flickering at the boundary

const uint8_t SPEED_FULL = 255;       // PWM 0-255
const uint8_t SPEED_SLOW = 130;       // raise if the wheel hums but doesn't turn
const uint8_t SPEED_STOP = 0;

const uint8_t RAMP_STEP          = 15;
const unsigned long RAMP_MS      = 20;
const unsigned long SENSOR_MS    = 60;
const unsigned long DISPLAY_MS   = 150;
const unsigned long BLINK_MS     = 120;
const uint8_t BLINK_COUNT        = 3;
const unsigned long ECHO_TIMEOUT = 30000UL;

// ---------------- State ----------------
enum MotorState { STATE_FULL, STATE_SLOW, STATE_STOP };

MotorState state   = STATE_FULL;
float distanceCm   = -1;     // -1 = no echo
float lastValidCm  = 999;    // last real reading, used when the echo disappears
bool sensorBlocked = false;  // true when something is too close for the sensor to measure
uint8_t currentPwm = 0;
uint8_t targetPwm  = SPEED_FULL;

unsigned long lastSensorMs  = 0;
unsigned long lastRampMs    = 0;
unsigned long lastDisplayMs = 0;
unsigned long lastBlinkMs   = 0;
uint8_t blinkTogglesLeft    = 0;

// ---------------- Ultrasonic ----------------
float readDistanceOnce() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  unsigned long duration = pulseIn(ECHO_PIN, HIGH, ECHO_TIMEOUT);
  if (duration == 0) return -1;
  return duration * 0.0343 / 2.0;
}

// Takes 3 readings and returns a safe value:
//   3 good readings -> the middle one
//   1-2 good        -> the closest one (safer)
//   0 good          -> -1 (no echo)
float readDistance() {
  float r[3];
  r[0] = readDistanceOnce(); delay(10);
  r[1] = readDistanceOnce(); delay(10);
  r[2] = readDistanceOnce();

  uint8_t good = 0;
  float closest = 9999;
  for (uint8_t i = 0; i < 3; i++) {
    if (r[i] > 0) {
      good++;
      if (r[i] < closest) closest = r[i];
    }
  }
  if (good == 0) return -1;
  if (good < 3) return closest;

  float x = r[0], y = r[1], z = r[2];
  return max(min(x, y), min(max(x, y), z));   // median
}

// ---------------- State logic ----------------
MotorState decideState(float d, MotorState prev) {
  if (sensorBlocked) return STATE_STOP;   // hand right on the sensor
  if (d < 0) return STATE_FULL;           // genuinely nothing in front

  float stopLimit = (prev == STATE_STOP) ? STOP_DISTANCE_CM + HYSTERESIS_CM : STOP_DISTANCE_CM;
  float slowLimit = (prev != STATE_FULL) ? SLOW_DISTANCE_CM + HYSTERESIS_CM : SLOW_DISTANCE_CM;

  if (d <= stopLimit) return STATE_STOP;
  if (d <= slowLimit) return STATE_SLOW;
  return STATE_FULL;
}

uint8_t pwmFor(MotorState s) {
  switch (s) {
    case STATE_SLOW: return SPEED_SLOW;
    case STATE_STOP: return SPEED_STOP;
    default:         return SPEED_FULL;
  }
}

// ---------------- LED blink ----------------
void startBlink() {
  blinkTogglesLeft = BLINK_COUNT * 2;
  digitalWrite(LED_PIN, HIGH);
  blinkTogglesLeft--;
  lastBlinkMs = millis();
}

void updateBlink(unsigned long now) {
  if (blinkTogglesLeft == 0) return;
  if (now - lastBlinkMs >= BLINK_MS) {
    lastBlinkMs = now;
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    blinkTogglesLeft--;
    if (blinkTogglesLeft == 0) digitalWrite(LED_PIN, LOW);
  }
}

// ---------------- Motor (L298N) ----------------
void applyMotorPwm(uint8_t pwm) {
  if (pwm == 0) {
    digitalWrite(DIR1_PIN, LOW);     // stop
    digitalWrite(DIR2_PIN, LOW);
  } else {
    digitalWrite(DIR1_PIN, HIGH);    // forward (swap HIGH/LOW to reverse)
    digitalWrite(DIR2_PIN, LOW);
  }
  analogWrite(EN_PIN, pwm);
}

void updateRamp(unsigned long now) {
  if (currentPwm == targetPwm || now - lastRampMs < RAMP_MS) return;
  lastRampMs = now;

  if (targetPwm == 0) {
    currentPwm = 0;                  // stop immediately
  } else if (currentPwm < targetPwm) {
    currentPwm = (targetPwm - currentPwm > RAMP_STEP) ? currentPwm + RAMP_STEP : targetPwm;
  } else {
    currentPwm = (currentPwm - targetPwm > RAMP_STEP) ? currentPwm - RAMP_STEP : targetPwm;
  }
  applyMotorPwm(currentPwm);
}

// ---------------- OLED ----------------
//  +--------------------------+
//  |      OBSTACLE ROBOT      |  size 1
//  |--------------------------|
//  | 15.2 cm                  |  size 2
//  | SLOW DOWN                |  size 2 (STOPPED! is shown inverted)
//  | SPD [#########.......]   |  speed bar
//  +--------------------------+
void updateDisplay(unsigned long now) {
  if (!oledOk || now - lastDisplayMs < DISPLAY_MS) return;
  lastDisplayMs = now;

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // Header
  display.setTextSize(1);
  display.setCursor(22, 0);
  display.print(F("OBSTACLE ROBOT"));
  display.drawFastHLine(0, 10, SCREEN_W, SSD1306_WHITE);

  // Distance
  display.setTextSize(2);
  display.setCursor(0, 14);
  if (sensorBlocked) {
    display.print(F("< 3 cm"));
  } else if (distanceCm < 0) {
    display.print(F("--- cm"));
  } else {
    display.print(distanceCm, 1);
    display.print(F(" cm"));
  }

  // Status
  switch (state) {
    case STATE_FULL:
      display.setCursor(0, 34);
      display.print(F("PATH CLEAR"));
      break;
    case STATE_SLOW:
      display.setCursor(0, 34);
      display.print(F("SLOW DOWN"));
      break;
    case STATE_STOP:
      display.fillRect(0, 32, SCREEN_W, 19, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
      display.setCursor(16, 34);
      display.print(F("STOPPED!"));
      display.setTextColor(SSD1306_WHITE);
      break;
  }

  // Speed bar
  display.setTextSize(1);
  display.setCursor(0, 55);
  display.print(F("SPD"));
  display.drawRect(22, 54, 106, 10, SSD1306_WHITE);
  uint8_t barW = (uint16_t)currentPwm * 102 / 255;
  if (barW > 0) display.fillRect(24, 56, barW, 6, SSD1306_WHITE);

  display.display();
}

// ---------------- Setup / loop ----------------
void setup() {
  Serial.begin(9600);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(EN_PIN, OUTPUT);
  pinMode(DIR1_PIN, OUTPUT);
  pinMode(DIR2_PIN, OUTPUT);

  applyMotorPwm(0);

  oledOk = display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  if (oledOk) {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(2);
    display.setCursor(16, 8);
    display.print(F("OBSTACLE"));
    display.setCursor(34, 28);
    display.print(F("ROBOT"));
    display.setTextSize(1);
    display.setCursor(31, 52);
    display.print(F("Starting..."));
    display.display();
  } else {
    // The motor logic still runs without the screen.
    Serial.println(F("OLED not found - check wiring or try address 0x3D"));
  }
  delay(1500);

  distanceCm = readDistance();
  if (distanceCm > 0) lastValidCm = distanceCm;
  state = decideState(distanceCm, STATE_FULL);
  targetPwm = pwmFor(state);
  startBlink();
}

void loop() {
  unsigned long now = millis();

  if (now - lastSensorMs >= SENSOR_MS) {
    lastSensorMs = now;
    distanceCm = readDistance();

    // No echo can mean "nothing in front" OR "hand covering the sensor".
    // If the last real reading was close, it's the second case -> stop.
    if (distanceCm < 0) {
      sensorBlocked = (lastValidCm <= SLOW_DISTANCE_CM);
    } else {
      sensorBlocked = (distanceCm < 3.0);   // closer than the sensor can measure well
      lastValidCm = distanceCm;
    }

    MotorState newState = decideState(distanceCm, state);
    if (newState != state) {
      state = newState;
      targetPwm = pwmFor(state);
      startBlink();                  // speed changed -> blink LED

      Serial.print(F("State -> "));
      Serial.print(state == STATE_FULL ? F("FULL") : state == STATE_SLOW ? F("SLOW") : F("STOP"));
      Serial.print(F("  dist="));
      Serial.println(distanceCm);
    }
  }

  updateRamp(now);
  updateBlink(now);
  updateDisplay(now);
}
