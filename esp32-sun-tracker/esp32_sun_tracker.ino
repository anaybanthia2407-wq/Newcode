/*
  ESP32 DevKit V1 Sun Tracker (Servo + LDR + 16x2 I2C LCD + LED)
  ---------------------------------------------------------------
  An LDR mounted on a servo looks for the brightest light source
  (the sun, or a phone flashlight for indoor demos) and turns to face it.
  The LCD shows what the tracker is doing, and the LED lights up
  whenever the servo motor is turning.

  How it tracks with a single LDR:
    1. SWEEP  - the servo sweeps 0..180 degrees, reading the LDR every few degrees.
    2. TURN   - it turns to the angle that gave the brightest reading.
    3. TRACK  - it watches the light level. Every so often it peeks a few
                degrees left and right and moves if either side is brighter
                (this follows the sun slowly across the sky).
    4. If the light drops sharply (flashlight moved or switched off,
       cloud, etc.) it sweeps again to find the light.

  Wiring (ESP32 DevKit V1):
    LDR      : 3V3 -> LDR -> GPIO34 -> 10k resistor -> GND
    Servo    : signal -> GPIO13, red -> VIN (5V), brown/black -> GND
    LED      : GPIO26 -> 220 ohm resistor -> LED anode (+), LED cathode (-) -> GND
    LCD I2C  : VCC -> VIN (5V), GND -> GND, SDA -> GPIO21, SCL -> GPIO22

  Required libraries (install via Arduino Library Manager):
    - ESP32Servo         (by Kevin Harrington)
    - LiquidCrystal I2C  (Frank de Brabander's GitHub version,
                          "Arduino-LiquidCrystal-I2C-library", which uses lcd.begin())
*/

#include <Wire.h>
#include <ESP32Servo.h>
#include <LiquidCrystal_I2C.h>

// ---- Pins ----
const int LDR_PIN = 34;    // ADC1 input-only pin
const int SERVO_PIN = 13;
const int LED_PIN = 26;

// ---- LCD settings ----
// Most 16x2 I2C backpacks are 0x27; some are 0x3F. Try the other if the screen stays blank.
const uint8_t LCD_ADDRESS = 0x27;
const int LCD_COLS = 16;
const int LCD_ROWS = 2;

// ---- Light settings (raw ADC readings, 0..4095) ----
// Open the Serial Monitor at 115200 baud to see live readings and tune these.
const int LIGHT_THRESHOLD = 1500;   // at or above this counts as "sun / flashlight found"
const int VERY_BRIGHT_LEVEL = 3200; // only used for the LCD label
const int DIM_LEVEL = 600;          // only used for the LCD label
const float LOST_RATIO = 0.70;      // re-sweep if light falls below 70% of the locked level
const int NUDGE_MARGIN = 40;        // a side must be this much brighter before we move to it

// ---- Servo settings ----
const int SERVO_MIN = 0;
const int SERVO_MAX = 180;
const int SCAN_STEP = 5;     // degrees between readings during a sweep
const int NUDGE_STEP = 5;    // degrees to peek left/right while tracking
const int MOVE_DELAY_MS = 15; // per degree while turning (lower = faster)
const int SETTLE_MS = 60;    // let the servo stop and the LDR settle before reading

// ---- Timing ----
const unsigned long DARK_RESCAN_MS = 20000; // sweep again this often while no light is found
const unsigned long NUDGE_MS = 20000;       // fine-tune toward the light this often
const unsigned long LCD_REFRESH_MS = 300;

const char DEGREE_CHAR = (char)223; // degree symbol on HD44780 LCDs

enum TrackerState { WAITING_FOR_LIGHT, TRACKING };

Servo tracker;
LiquidCrystal_I2C lcd(LCD_ADDRESS, LCD_COLS, LCD_ROWS);

TrackerState state = WAITING_FOR_LIGHT;
int currentAngle = 90;
int lockedLevel = 0;
unsigned long lastScanMs = 0;
unsigned long lastNudgeMs = 0;
unsigned long lastLcdMs = 0;

// Averages several ADC samples to smooth out noise.
int readLight() {
  long sum = 0;
  for (int i = 0; i < 16; i++) {
    sum += analogRead(LDR_PIN);
    delayMicroseconds(200);
  }
  return sum / 16;
}

int toPercent(int raw) {
  return map(raw, 0, 4095, 0, 100);
}

const char *lightLabel(int raw) {
  if (raw >= VERY_BRIGHT_LEVEL) return "Very bright";
  if (raw >= LIGHT_THRESHOLD) return "Bright";
  if (raw >= DIM_LEVEL) return "Dim";
  return "Dark";
}

// Writes a full LCD row, padding with spaces so old text is overwritten
// without calling lcd.clear() (which makes the screen flicker).
void showLine(int row, const char *text) {
  char buf[LCD_COLS + 1];
  snprintf(buf, sizeof(buf), "%-*s", LCD_COLS, text);
  lcd.setCursor(0, row);
  lcd.print(buf);
}

void showStatus(const char *top, const char *bottom) {
  showLine(0, top);
  showLine(1, bottom);
}

// Turns the servo one degree at a time for smooth motion.
// The LED stays lit for as long as the motor is turning.
void turnTo(int target) {
  target = constrain(target, SERVO_MIN, SERVO_MAX);
  if (target != currentAngle) {
    digitalWrite(LED_PIN, HIGH);
    int dir = (target > currentAngle) ? 1 : -1;
    while (currentAngle != target) {
      currentAngle += dir;
      tracker.write(currentAngle);
      delay(MOVE_DELAY_MS);
    }
  }
  digitalWrite(LED_PIN, LOW);
}

// Sweeps the whole range and returns the angle with the brightest reading.
int sweepForLight(int &bestLevel) {
  showStatus("Scanning for", "the sun...");
  Serial.println("Sweeping for light...");

  // Start from whichever end is closer to save time.
  int start = (currentAngle <= (SERVO_MIN + SERVO_MAX) / 2) ? SERVO_MIN : SERVO_MAX;
  int step = (start == SERVO_MIN) ? SCAN_STEP : -SCAN_STEP;
  turnTo(start);

  digitalWrite(LED_PIN, HIGH); // motor is turning for the whole sweep
  bestLevel = -1;
  int bestAngle = currentAngle;
  for (int a = start; a >= SERVO_MIN && a <= SERVO_MAX; a += step) {
    currentAngle = a;
    tracker.write(a);
    delay(SETTLE_MS);

    int level = readLight();
    if (level > bestLevel) {
      bestLevel = level;
      bestAngle = a;
    }

    char line[LCD_COLS + 1];
    snprintf(line, sizeof(line), "Ang:%3d Lvl:%3d%%", a, toPercent(level));
    showLine(1, line);
    Serial.printf("  angle=%3d raw=%4d\n", a, level);
  }
  digitalWrite(LED_PIN, LOW);

  Serial.printf("Brightest: angle=%d raw=%d\n", bestAngle, bestLevel);
  return bestAngle;
}

// Full search: sweep, then turn to the brightest direction if it is bright enough.
void scanAndTurn() {
  int bestLevel;
  int bestAngle = sweepForLight(bestLevel);
  lastScanMs = millis();

  if (bestLevel < LIGHT_THRESHOLD) {
    state = WAITING_FOR_LIGHT;
    showStatus("No sun found", "Waiting...");
    Serial.println("No bright light found, waiting.");
    delay(1000);
    return;
  }

  char line[LCD_COLS + 1];
  snprintf(line, sizeof(line), "Turning to %d%c", bestAngle, DEGREE_CHAR);
  showStatus("Sun found!", line);
  turnTo(bestAngle);

  lockedLevel = readLight();
  lastNudgeMs = millis();
  state = TRACKING;
  showStatus("Facing the sun", "Locked on!");
  Serial.printf("Locked on at %d deg, raw=%d\n", currentAngle, lockedLevel);
  delay(1000);
}

// Peeks a few degrees either side and moves there if it is brighter.
// This lets the tracker follow slow movement such as the sun crossing the sky.
void nudgeTowardLight() {
  showLine(0, "Fine tuning...");

  int origin = currentAngle;
  int bestAngle = origin;
  int bestLevel = readLight();
  const int offsets[] = { -NUDGE_STEP, NUDGE_STEP };
  for (int offset : offsets) {
    int a = constrain(origin + offset, SERVO_MIN, SERVO_MAX);
    if (a == origin) continue;
    turnTo(a);
    delay(SETTLE_MS);
    int level = readLight();
    if (level > bestLevel + NUDGE_MARGIN) {
      bestLevel = level;
      bestAngle = a;
    }
  }
  turnTo(bestAngle);

  // Refresh the reference level so slow, natural dimming doesn't add up
  // to a needless full sweep.
  lockedLevel = readLight();
  if (bestAngle != origin) {
    Serial.printf("Nudged %d -> %d deg\n", origin, bestAngle);
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  analogReadResolution(12);

  // LCD uses the default ESP32 I2C pins: SDA = GPIO21, SCL = GPIO22.
  // This library takes the size from the constructor, so begin() has no arguments.
  // (The older Library Manager version calls this lcd.init() instead.)
  lcd.begin();
  lcd.backlight();
  showStatus("ESP32 Sun", "Tracker ready");

  ESP32PWM::allocateTimer(0);
  tracker.setPeriodHertz(50);
  tracker.attach(SERVO_PIN, 500, 2400); // pulse range for SG90/MG90S-style servos
  tracker.write(currentAngle);

  // Quick LED self-test so you can see it is wired correctly.
  for (int i = 0; i < 3; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(150);
    digitalWrite(LED_PIN, LOW);
    delay(150);
  }
  delay(1000);

  scanAndTurn();
}

void loop() {
  int level = readLight();
  unsigned long now = millis();

  if (state == WAITING_FOR_LIGHT) {
    // Light appeared in front of the sensor, or it's time for another look around.
    if (level >= LIGHT_THRESHOLD || now - lastScanMs >= DARK_RESCAN_MS) {
      scanAndTurn();
      return;
    }
  } else {
    if (level < lockedLevel * LOST_RATIO) {
      showStatus("Sun moved/lost!", "Searching...");
      Serial.printf("Light dropped (raw=%d, locked=%d)\n", level, lockedLevel);
      delay(500);
      scanAndTurn();
      return;
    }
    if (level > lockedLevel) {
      lockedLevel = level;
    }
    if (now - lastNudgeMs >= NUDGE_MS) {
      nudgeTowardLight();
      lastNudgeMs = millis();
      return;
    }
  }

  if (now - lastLcdMs >= LCD_REFRESH_MS) {
    lastLcdMs = now;
    char bottom[LCD_COLS + 1];
    if (state == TRACKING) {
      showLine(0, "Facing the sun");
      snprintf(bottom, sizeof(bottom), "Ang:%3d Lvl:%3d%%", currentAngle, toPercent(level));
    } else {
      showLine(0, "Waiting for sun");
      snprintf(bottom, sizeof(bottom), "%-12s%3d%%", lightLabel(level), toPercent(level));
    }
    showLine(1, bottom);
    Serial.printf("state=%s angle=%d raw=%d\n",
                  state == TRACKING ? "TRACKING" : "WAITING", currentAngle, level);
  }
}
