/*
 * Obstacle-Aware Motor Controller - Arduino Uno (NO motor driver version)
 *
 * The L298N is replaced by one transistor switch:
 *   D5 -> 1k resistor -> TIP120 base
 *   TIP120 collector -> motor (-) ; motor (+) -> 9V battery (+)
 *   TIP120 emitter -> GND (battery - and Arduino GND joined)
 *   1N4007 diode across the motor, stripe (cathode) on the 9V (+) side
 *
 * Hardware:
 *   - Arduino Uno
 *   - HC-SR04 ultrasonic sensor
 *   - 16x2 LCD with I2C backpack (PCF8574, address 0x27 or 0x3F)
 *   - BO DC motor + wheel, 9V battery
 *   - TIP120 transistor (or IRLZ44N MOSFET), 1k resistor, 1N4007 diode
 *   - LED + 220 ohm resistor
 *
 * Behaviour:
 *   distance > 20 cm        -> wheel runs at FULL speed
 *   10 cm < distance <= 20  -> wheel SLOWS DOWN
 *   distance <= 10 cm       -> wheel STOPS
 *   The LCD shows the distance and the current state.
 *   The LED blinks every time the wheel's speed changes.
 *
 * Library needed (Arduino IDE > Library Manager):
 *   "LiquidCrystal I2C" by Frank de Brabander
 */

#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ---------------- Pin map ----------------
const uint8_t TRIG_PIN = 9;   // HC-SR04 TRIG
const uint8_t ECHO_PIN = 10;  // HC-SR04 ECHO
const uint8_t LED_PIN  = 2;   // status LED (through 220 ohm resistor)
const uint8_t MOTOR_PIN = 5;  // PWM -> 1k resistor -> TIP120 base
// LCD I2C: SDA -> A4, SCL -> A5

// ---------------- Tunable settings ----------------
const float SLOW_DISTANCE_CM = 20.0;  // start slowing at/below this
const float STOP_DISTANCE_CM = 10.0;  // stop at/below this
const float HYSTERESIS_CM    = 1.0;   // stops the state flickering at a boundary

const uint8_t SPEED_FULL = 255;  // PWM 0-255
const uint8_t SPEED_SLOW = 150;  // transistor drops ~1.5V, so slow needs a bit more PWM
const uint8_t SPEED_STOP = 0;

const uint8_t RAMP_STEP          = 15;   // PWM change per ramp tick (smooth speed change)
const unsigned long RAMP_MS      = 20;   // ramp tick interval
const unsigned long SENSOR_MS    = 60;   // HC-SR04 needs ~60 ms between pings
const unsigned long LCD_MS       = 250;  // LCD refresh interval
const unsigned long BLINK_MS     = 120;  // LED on/off time during a blink
const uint8_t BLINK_COUNT        = 3;    // blinks per speed change
const unsigned long ECHO_TIMEOUT = 30000UL; // us, ~5 m max range

LiquidCrystal_I2C lcd(0x27, 16, 2);  // change to 0x3F if the screen stays blank

// ---------------- State ----------------
enum MotorState { STATE_FULL, STATE_SLOW, STATE_STOP };

MotorState state       = STATE_FULL;
float distanceCm       = -1;    // -1 = no echo / out of range
uint8_t currentPwm     = 0;
uint8_t targetPwm      = SPEED_FULL;

unsigned long lastSensorMs = 0;
unsigned long lastRampMs   = 0;
unsigned long lastLcdMs    = 0;
unsigned long lastBlinkMs  = 0;
uint8_t blinkTogglesLeft   = 0;
bool lcdStateDirty         = true;

// ---------------- Ultrasonic ----------------
float readDistanceOnce() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  unsigned long duration = pulseIn(ECHO_PIN, HIGH, ECHO_TIMEOUT);
  if (duration == 0) return -1;            // no echo -> nothing in range
  return duration * 0.0343 / 2.0;          // speed of sound 343 m/s, round trip
}

// Median of 3 readings rejects the occasional bogus echo.
float readDistance() {
  float a = readDistanceOnce(); delay(10);
  float b = readDistanceOnce(); delay(10);
  float c = readDistanceOnce();

  // Treat "no echo" as very far so it sorts last.
  float x = (a < 0) ? 9999 : a;
  float y = (b < 0) ? 9999 : b;
  float z = (c < 0) ? 9999 : c;

  float m = max(min(x, y), min(max(x, y), z));
  return (m >= 9999) ? -1 : m;
}

// ---------------- State logic ----------------
MotorState decideState(float d, MotorState prev) {
  if (d < 0) return STATE_FULL;  // nothing detected -> path clear

  // Hysteresis: once in a slower state, require moving a little further
  // away before speeding back up.
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

// ---------------- LED blink (non-blocking) ----------------
void startBlink() {
  blinkTogglesLeft = BLINK_COUNT * 2;  // each blink = on + off
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

// ---------------- Motor ----------------
void applyMotorPwm(uint8_t pwm) {
  // One direction only. PWM 0 turns the transistor off and the wheel coasts to a stop.
  analogWrite(MOTOR_PIN, pwm);
}

void updateRamp(unsigned long now) {
  if (currentPwm == targetPwm || now - lastRampMs < RAMP_MS) return;
  lastRampMs = now;

  // Stopping is immediate for safety; speeding up / slowing down is smooth.
  if (targetPwm == 0) {
    currentPwm = 0;
  } else if (currentPwm < targetPwm) {
    currentPwm = (targetPwm - currentPwm > RAMP_STEP) ? currentPwm + RAMP_STEP : targetPwm;
  } else {
    currentPwm = (currentPwm - targetPwm > RAMP_STEP) ? currentPwm - RAMP_STEP : targetPwm;
  }
  applyMotorPwm(currentPwm);
}

// ---------------- LCD ----------------
void printPadded(uint8_t row, const char *text) {
  char buf[17];
  snprintf(buf, sizeof(buf), "%-16s", text);  // pad to 16 chars to wipe old text
  lcd.setCursor(0, row);
  lcd.print(buf);
}

void updateLcd(unsigned long now) {
  if (now - lastLcdMs < LCD_MS) return;
  lastLcdMs = now;

  // Row 0: distance
  char line[17];
  if (distanceCm < 0) {
    snprintf(line, sizeof(line), "Dist: ---  cm");
  } else {
    char num[8];
    dtostrf(distanceCm, 5, 1, num);  // snprintf on AVR has no %f
    snprintf(line, sizeof(line), "Dist:%s cm", num);
  }
  printPadded(0, line);

  // Row 1: state message (only rewritten when it changes)
  if (lcdStateDirty) {
    lcdStateDirty = false;
    switch (state) {
      case STATE_FULL: printPadded(1, "Clear: Full Spd"); break;
      case STATE_SLOW: printPadded(1, "Obstacle! Slow"); break;
      case STATE_STOP: printPadded(1, "STOP! Too Close"); break;
    }
  }
}

// ---------------- Setup / loop ----------------
void setup() {
  Serial.begin(9600);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(MOTOR_PIN, OUTPUT);

  applyMotorPwm(0);

  lcd.init();
  lcd.backlight();
  printPadded(0, "Obstacle Robot");
  printPadded(1, "Starting...");
  delay(1500);
  lcd.clear();

  // Take a first reading so the wheel doesn't start toward a close obstacle.
  distanceCm = readDistance();
  state = decideState(distanceCm, STATE_FULL);
  targetPwm = pwmFor(state);
  startBlink();  // speed goes from 0 to its first value -> blink
  lcdStateDirty = true;
}

void loop() {
  unsigned long now = millis();

  if (now - lastSensorMs >= SENSOR_MS) {
    lastSensorMs = now;
    distanceCm = readDistance();

    MotorState newState = decideState(distanceCm, state);
    if (newState != state) {
      state = newState;
      targetPwm = pwmFor(state);
      startBlink();          // speed is changing -> blink LED
      lcdStateDirty = true;

      Serial.print(F("State -> "));
      Serial.print(state == STATE_FULL ? F("FULL") : state == STATE_SLOW ? F("SLOW") : F("STOP"));
      Serial.print(F("  dist="));
      Serial.println(distanceCm);
    }
  }

  updateRamp(now);
  updateBlink(now);
  updateLcd(now);
}
