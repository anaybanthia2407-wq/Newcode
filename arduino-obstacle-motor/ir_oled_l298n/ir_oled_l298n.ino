/*
 * Obstacle-Aware Motor Controller - Arduino Uno + FC-51 IR sensor + L298N + OLED
 *
 * Parts: Arduino Uno, FC-51 IR obstacle sensor, 0.96" 128x64 I2C OLED (SSD1306),
 *        L298N motor driver, one BO DC motor + wheel, LED + 220 ohm resistor
 *
 * The FC-51 only says "obstacle" or "no obstacle" (it cannot measure distance), so:
 *   no obstacle                     -> wheel runs at FULL speed
 *   obstacle detected               -> wheel SLOWS DOWN
 *   obstacle still there after 1 s  -> wheel STOPS
 *   obstacle removed                -> back to FULL speed
 *   OLED shows sensor + status + speed bar. LED blinks whenever the speed changes.
 *
 * Set the detection distance with the blue knob on the FC-51 (see notes).
 *
 * Power (one 9-12V DC adapter, 1A or more):
 *   Adapter -> Arduino DC jack, Arduino VIN -> L298N 12V, Arduino GND -> L298N GND
 *
 * Libraries needed: "Adafruit SSD1306" and "Adafruit GFX Library"
 */

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ---------------- Pin map ----------------
const uint8_t IR_PIN    = 9;   // FC-51 OUT  (LOW = obstacle)
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
const unsigned long STOP_AFTER_MS  = 1000;  // obstacle held this long -> stop
const unsigned long CLEAR_AFTER_MS = 300;   // must be clear this long before speeding up

const uint8_t SPEED_FULL = 255;       // PWM 0-255
const uint8_t SPEED_SLOW = 130;       // raise if the wheel hums but doesn't turn
const uint8_t SPEED_STOP = 0;

const uint8_t RAMP_STEP          = 15;
const unsigned long RAMP_MS      = 20;
const unsigned long SENSOR_MS    = 10;
const unsigned long DISPLAY_MS   = 150;
const unsigned long BLINK_MS     = 120;
const uint8_t BLINK_COUNT        = 3;

// ---------------- State ----------------
enum MotorState { STATE_FULL, STATE_SLOW, STATE_STOP };

MotorState state   = STATE_FULL;
bool obstacle      = false;
unsigned long obstacleSinceMs = 0;   // when the current obstacle was first seen
unsigned long lastSeenMs      = 0;   // last time the sensor saw it

uint8_t currentPwm = 0;
uint8_t targetPwm  = SPEED_FULL;

unsigned long lastSensorMs  = 0;
unsigned long lastRampMs    = 0;
unsigned long lastDisplayMs = 0;
unsigned long lastBlinkMs   = 0;
uint8_t blinkTogglesLeft    = 0;

// ---------------- IR sensor ----------------
void updateObstacle(unsigned long now) {
  bool seen = (digitalRead(IR_PIN) == LOW);   // FC-51 pulls OUT LOW on obstacle

  if (seen) {
    lastSeenMs = now;
    if (!obstacle) {
      obstacle = true;
      obstacleSinceMs = now;
    }
  } else if (obstacle && now - lastSeenMs >= CLEAR_AFTER_MS) {
    obstacle = false;                          // clear only after a short steady gap
  }
}

MotorState decideState(unsigned long now) {
  if (!obstacle) return STATE_FULL;
  if (now - obstacleSinceMs >= STOP_AFTER_MS) return STATE_STOP;
  return STATE_SLOW;
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

  // Sensor reading
  display.setTextSize(2);
  display.setCursor(0, 14);
  display.print(obstacle ? F("IR: OBJECT") : F("IR: CLEAR"));

  // Status
  switch (state) {
    case STATE_FULL:
      display.setCursor(0, 34);
      display.print(F("FULL SPEED"));
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

  pinMode(IR_PIN, INPUT);
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
    Serial.println(F("OLED not found - check wiring or try address 0x3D"));
  }
  delay(1500);

  unsigned long now = millis();
  updateObstacle(now);
  state = decideState(now);
  targetPwm = pwmFor(state);
  startBlink();
}

void loop() {
  unsigned long now = millis();

  if (now - lastSensorMs >= SENSOR_MS) {
    lastSensorMs = now;
    updateObstacle(now);

    MotorState newState = decideState(now);
    if (newState != state) {
      state = newState;
      targetPwm = pwmFor(state);
      startBlink();                  // speed changed -> blink LED

      Serial.print(F("State -> "));
      Serial.println(state == STATE_FULL ? F("FULL") : state == STATE_SLOW ? F("SLOW") : F("STOP"));
    }
  }

  updateRamp(now);
  updateBlink(now);
  updateDisplay(now);
}
