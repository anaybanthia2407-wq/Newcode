/*
  ESP32 DevKit V1 Solar Tracker (SG90 servo + 3-pin LDR module + 0.96" OLED)
  --------------------------------------------------------------------------
  A 3-pin LDR module mounted on an SG90 servo finds the sun (or a phone
  flashlight for indoor demos) and turns to face it. The OLED shows what the
  tracker is doing, the servo angle, and a dial with a needle pointing where
  the sensor is facing.

  A 3-pin LDR module only says BRIGHT or DARK (its DO pin), not how bright.
  So the tracker works like this:
    1. SWEEP  - the servo sweeps 0..180 degrees and marks every angle where
                the module says BRIGHT.
    2. TURN   - it turns to the middle of the widest bright patch, which is
                where the light source is.
    3. TRACK  - every 30 s it finds both edges of the bright patch around it
                and re-centres between them (this follows the sun).
    4. If the module goes DARK (light moved or switched off), it sweeps again.

  Wiring (ESP32 DevKit V1):
    OLED (SSD1306 I2C) : VCC -> 3V3, GND -> GND, SDA -> GPIO21, SCL -> GPIO22
    LDR module         : VCC -> 3V3 (not 5V), GND -> GND, DO -> GPIO34
    SG90 servo         : signal (orange) -> GPIO13, red -> VIN (5V), brown -> GND

  Required libraries (install via Arduino Library Manager):
    - Adafruit SSD1306   (also installs Adafruit GFX Library)
    - ESP32Servo         (by Kevin Harrington)
*/

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ESP32Servo.h>

// ---- Pins ----
const int LDR_DO_PIN = 34;
const int SERVO_PIN = 13;

// Most LDR modules pull DO LOW when it is bright (the small DO LED on the
// module turns on). If yours is the other way round, change this to HIGH.
const int BRIGHT_LEVEL = LOW;

// ---- OLED settings ----
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C   // some modules use 0x3D

// ---- Servo settings ----
const int SERVO_MIN = 0;
const int SERVO_MAX = 180;
const int SCAN_STEP = 3;      // degrees between checks during a sweep
const int EDGE_STEP = 2;      // degrees per step when looking for patch edges
const int EDGE_LIMIT = 45;    // how far either side to look for an edge
const int MOVE_DELAY_MS = 15; // per degree while turning (lower = faster)
const int SETTLE_MS = 50;     // let the servo stop before reading the sensor

// ---- Timing ----
const unsigned long DARK_RESCAN_MS = 20000; // sweep again this often while no sun is found
const unsigned long RECENTER_MS = 30000;    // re-centre on the light this often
const unsigned long DARK_CONFIRM_MS = 400;  // ignore dark blips shorter than this
const unsigned long DISPLAY_MS = 250;

const int SCAN_POINTS = (SERVO_MAX - SERVO_MIN) / SCAN_STEP + 1;

enum TrackerState { WAITING_FOR_SUN, TRACKING };

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
Servo tracker;

TrackerState state = WAITING_FOR_SUN;
int currentAngle = 90;
bool lastBright = false;
bool scanBright[SCAN_POINTS];   // what the last sweep saw, drawn on the dial
char statusText[22] = "";
unsigned long lastScanMs = 0;
unsigned long lastRecenterMs = 0;
unsigned long lastDisplayMs = 0;

// Reads DO a few times and takes the majority, to ignore flicker.
bool isBright() {
  int hits = 0;
  for (int i = 0; i < 5; i++) {
    if (digitalRead(LDR_DO_PIN) == BRIGHT_LEVEL) hits++;
    delay(2);
  }
  return hits >= 3;
}

// Small sun icon: filled with rays when bright, an empty circle when dark.
void drawSun(int x, int y, bool bright) {
  if (!bright) {
    display.drawCircle(x, y, 3, SSD1306_WHITE);
    return;
  }
  display.fillCircle(x, y, 3, SSD1306_WHITE);
  for (int a = 0; a < 360; a += 45) {
    float rad = a * DEG_TO_RAD;
    display.drawLine(x + 5 * cos(rad), y - 5 * sin(rad),
                     x + 7 * cos(rad), y - 7 * sin(rad), SSD1306_WHITE);
  }
}

void drawScreen() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(25, 0);
  display.print("SOLAR TRACKER");
  display.drawFastHLine(0, 10, SCREEN_WIDTH, SSD1306_WHITE);

  display.setCursor(0, 14);
  display.print(statusText);
  display.setCursor(0, 24);
  display.printf("Angle:%3d  Sun:%s", currentAngle, lastBright ? "YES" : "NO");

  // Half-circle dial: 0 deg on the right, 180 deg on the left.
  const int cx = 64, cy = 63, r = 22;
  for (int a = 0; a <= 180; a += 15) {
    float rad = a * DEG_TO_RAD;
    display.drawPixel(cx + r * cos(rad), cy - r * sin(rad), SSD1306_WHITE);
  }
  // Ticks outside the dial where the last sweep saw light.
  for (int i = 0; i < SCAN_POINTS; i++) {
    if (!scanBright[i]) continue;
    float rad = (SERVO_MIN + i * SCAN_STEP) * DEG_TO_RAD;
    display.drawLine(cx + (r + 3) * cos(rad), cy - (r + 3) * sin(rad),
                     cx + (r + 6) * cos(rad), cy - (r + 6) * sin(rad), SSD1306_WHITE);
  }
  // Needle showing where the sensor is facing, with the sun icon at its tip.
  float rad = currentAngle * DEG_TO_RAD;
  int tipX = cx + (r - 6) * cos(rad);
  int tipY = cy - (r - 6) * sin(rad);
  display.drawLine(cx, cy, tipX, tipY, SSD1306_WHITE);
  drawSun(tipX, tipY, lastBright);

  display.display();
  lastDisplayMs = millis();
}

void setStatus(const char *text) {
  snprintf(statusText, sizeof(statusText), "%s", text);
  Serial.println(text);
  drawScreen();
}

// Turns the servo one degree at a time for smooth motion,
// redrawing the dial every few degrees so the needle moves too.
void turnTo(int target) {
  target = constrain(target, SERVO_MIN, SERVO_MAX);
  int dir = (target > currentAngle) ? 1 : -1;
  while (currentAngle != target) {
    currentAngle += dir;
    tracker.write(currentAngle);
    delay(MOVE_DELAY_MS);
    if (currentAngle % 6 == 0) drawScreen();
  }
  drawScreen();
}

// Sweeps the whole range and returns the middle of the widest bright patch,
// or -1 if the sensor never saw bright light.
int sweepForSun() {
  setStatus("Scanning for sun...");
  for (int i = 0; i < SCAN_POINTS; i++) scanBright[i] = false;

  // Start from whichever end is closer to save time.
  bool upward = currentAngle <= (SERVO_MIN + SERVO_MAX) / 2;
  turnTo(upward ? SERVO_MIN : SERVO_MAX);

  for (int n = 0; n < SCAN_POINTS; n++) {
    int i = upward ? n : SCAN_POINTS - 1 - n;
    currentAngle = SERVO_MIN + i * SCAN_STEP;
    tracker.write(currentAngle);
    delay(SETTLE_MS);
    lastBright = isBright();
    scanBright[i] = lastBright;
    drawScreen();
    Serial.printf("  angle=%3d %s\n", currentAngle, lastBright ? "BRIGHT" : "dark");
  }

  int bestStart = -1, bestLen = 0, runStart = 0, runLen = 0;
  for (int i = 0; i < SCAN_POINTS; i++) {
    if (scanBright[i]) {
      if (runLen == 0) runStart = i;
      runLen++;
      if (runLen > bestLen) {
        bestLen = runLen;
        bestStart = runStart;
      }
    } else {
      runLen = 0;
    }
  }
  if (bestLen == 0) return -1;
  return SERVO_MIN + (bestStart * 2 + bestLen - 1) * SCAN_STEP / 2;
}

// Full search: sweep, then turn to face the sun if one was found.
void scanAndTurn() {
  int target = sweepForSun();
  lastScanMs = millis();

  if (target < 0) {
    state = WAITING_FOR_SUN;
    setStatus("No sun found");
    delay(1000);
    setStatus("Waiting for sun...");
    return;
  }

  char text[22];
  snprintf(text, sizeof(text), "Sun found! Go %d", target);
  setStatus(text);
  turnTo(target);
  lastBright = isBright();
  state = TRACKING;
  lastRecenterMs = millis();
  setStatus("Facing the sun");
}

// Finds both edges of the bright patch around the current angle and moves
// to the middle. Repeating this keeps the tracker centred as the sun moves.
void recenter() {
  setStatus("Fine tuning...");
  int origin = currentAngle;

  int low = origin;
  while (low - EDGE_STEP >= max(SERVO_MIN, origin - EDGE_LIMIT)) {
    turnTo(low - EDGE_STEP);
    delay(SETTLE_MS);
    if (!isBright()) break;
    low -= EDGE_STEP;
  }

  int high = origin;
  while (high + EDGE_STEP <= min(SERVO_MAX, origin + EDGE_LIMIT)) {
    turnTo(high + EDGE_STEP);
    delay(SETTLE_MS);
    if (!isBright()) break;
    high += EDGE_STEP;
  }

  turnTo((low + high) / 2);
  lastBright = isBright();
  Serial.printf("Bright patch %d..%d, centred at %d\n", low, high, currentAngle);
  setStatus("Facing the sun");
}

void setup() {
  Serial.begin(115200);
  pinMode(LDR_DO_PIN, INPUT);

  // OLED uses the default ESP32 I2C pins: SDA = GPIO21, SCL = GPIO22.
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println("OLED not found - check wiring and SCREEN_ADDRESS");
  }
  Wire.setClock(400000);  // faster I2C so the dial redraws smoothly
  display.clearDisplay(); // wipes whatever the previous sketch left on screen
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(10, 12);
  display.print("SOLAR");
  display.setCursor(10, 34);
  display.print("TRACKER");
  display.display();

  ESP32PWM::allocateTimer(0);
  tracker.setPeriodHertz(50);
  tracker.attach(SERVO_PIN, 500, 2400); // pulse range for SG90 servos
  tracker.write(currentAngle);
  delay(1500);

  scanAndTurn();
}

void loop() {
  lastBright = isBright();
  unsigned long now = millis();

  if (state == WAITING_FOR_SUN) {
    // Light reached the sensor, or it's time for another look around.
    if (lastBright || now - lastScanMs >= DARK_RESCAN_MS) {
      scanAndTurn();
      return;
    }
  } else {
    if (!lastBright) {
      delay(DARK_CONFIRM_MS);
      if (!isBright()) {
        setStatus("Sun moved/lost!");
        delay(500);
        scanAndTurn();
        return;
      }
      lastBright = true;
    }
    if (now - lastRecenterMs >= RECENTER_MS) {
      recenter();
      lastRecenterMs = millis();
      return;
    }
  }

  if (now - lastDisplayMs >= DISPLAY_MS) {
    drawScreen();
  }
}
