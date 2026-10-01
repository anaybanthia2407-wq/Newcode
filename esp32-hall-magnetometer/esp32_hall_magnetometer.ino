/*
 * ESP32 Hall-Effect Magnetic Field Detector
 * Board : ESP32 DevKit V1 (ESP32-WROOM-32)
 *
 * Uses the ESP32's built-in Hall-effect sensor to detect a nearby magnetic
 * field. An external LED (plus the on-board LED) blinks while a field is
 * present (faster for a stronger field) and a 16x2 I2C LCD shows the magnetic flux density in its
 * SI unit, the tesla (displayed as mT, or uT for weak fields).
 *
 * IMPORTANT: hallRead() only exists in Arduino-ESP32 core 1.x / 2.x.
 * It was removed in core 3.x, so install "esp32 by Espressif" version 2.0.17
 * in the Boards Manager.
 *
 * The Hall sensor is read internally through GPIO36 (VP) and GPIO39 (VN):
 * leave both pins unconnected.
 *
 * External LED wiring:
 *   GPIO4 (D4) -> 220 ohm resistor -> LED long leg (+)
 *   LED short leg (-) -> GND
 *
 * Serial commands (115200 baud, newline terminated):
 *   z          re-zero: measure the background field with no magnet nearby
 *   c <mT>     calibrate: hold a magnet of known field <mT> at the sensor,
 *              then send e.g. "c 25" to compute the counts-to-mT factor
 *   s          print current settings
 * Calibration is stored in flash and survives a reboot.
 *
 * The LCD's I2C address is found automatically at startup, and the sketch
 * works with both common "LiquidCrystal_I2C" libraries (init() or begin()).
 */

#if defined(__has_include)
#if __has_include(<esp_arduino_version.h>)
#include <esp_arduino_version.h>
#endif
#endif
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
#error "hallRead() was removed in Arduino-ESP32 core 3.x. Install core 2.0.17 in Boards Manager."
#endif

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Preferences.h>

// ---------------- Hardware ----------------
const uint8_t EXT_LED_PIN     = 4;  // External LED (via 220 ohm resistor to GND)
const uint8_t ONBOARD_LED_PIN = 2;  // On-board blue LED on DevKit V1
const uint8_t LCD_ADDR_DEFAULT = 0x27;  // Used only if the I2C scan finds nothing
const uint8_t LCD_COLS  = 16;
const uint8_t LCD_ROWS  = 2;
const uint8_t I2C_SDA   = 21;
const uint8_t I2C_SCL   = 22;

// ---------------- Measurement ----------------
const int   SAMPLES_PER_READING = 64;    // averaging, the raw sensor is noisy
const int   ZERO_SAMPLES        = 40;    // readings averaged for the zero offset
const float EMA_ALPHA           = 0.3f;  // display smoothing (0..1, higher = faster)

// Default sensitivity, approximate and varies from chip to chip.
// Run the "c <mT>" calibration for accurate readings.
const float DEFAULT_MT_PER_COUNT = 0.10f;

// Detection threshold with hysteresis, in mT
const float DETECT_ON_MT  = 1.5f;
const float DETECT_OFF_MT = 1.0f;

// LED blink period range (ms): slow for weak fields, fast for strong ones
const uint32_t BLINK_SLOW_MS = 800;
const uint32_t BLINK_FAST_MS = 60;
const float    BLINK_FULL_SCALE_MT = 20.0f;  // field at which blinking is fastest

const uint32_t LCD_UPDATE_MS = 250;

LiquidCrystal_I2C *lcd = nullptr;
Preferences prefs;

float zeroOffset   = 0.0f;   // background reading in raw counts
float mTPerCount   = DEFAULT_MT_PER_COUNT;
float fieldmT      = 0.0f;   // smoothed field, signed
bool  fieldPresent = false;

bool     ledState      = false;
uint32_t lastToggleMs  = 0;
uint32_t lastLcdMs     = 0;

// Calls init() or begin(), whichever the installed LiquidCrystal_I2C provides.
// Kept inside a struct because the Arduino IDE breaks top-level templates.
struct LcdStarter {
  template <typename T>
  static auto start(T &l, int) -> decltype(l.init(), void()) { l.init(); }
  template <typename T>
  static auto start(T &l, long) -> decltype(l.begin(), void()) { l.begin(); }
};

// Returns the first I2C device address found, or 0 if none
uint8_t findLcdAddress() {
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) return addr;
  }
  return 0;
}

void setupLcd() {
  uint8_t addr = findLcdAddress();
  if (addr == 0) {
    Serial.println("No I2C LCD found! Check SDA->D21, SCL->D22, VCC->VIN, GND->GND.");
    addr = LCD_ADDR_DEFAULT;
  } else {
    Serial.printf("LCD found at I2C address 0x%02X\n", addr);
  }
  lcd = new LiquidCrystal_I2C(addr, LCD_COLS, LCD_ROWS);
  LcdStarter::start(*lcd, 0);
  lcd->backlight();
  lcd->clear();
}

// Prints text on one LCD row, padded to the full width so old text is erased
void lcdLine(uint8_t row, const char *text) {
  char buf[LCD_COLS + 1];
  snprintf(buf, sizeof(buf), "%-16s", text);
  lcd->setCursor(0, row);
  lcd->print(buf);
}

// Averaged raw Hall reading
float readHallRaw(int samples = SAMPLES_PER_READING) {
  long sum = 0;
  for (int i = 0; i < samples; i++) {
    sum += hallRead();
  }
  return (float)sum / samples;
}

void zeroSensor() {
  lcdLine(0, "Zeroing sensor..");
  lcdLine(1, "Keep magnet away");
  Serial.println("Zeroing: keep magnets away from the board...");

  float sum = 0;
  for (int i = 0; i < ZERO_SAMPLES; i++) {
    sum += readHallRaw();
    delay(10);
  }
  zeroOffset = sum / ZERO_SAMPLES;
  fieldmT = 0.0f;
  prefs.putFloat("zero", zeroOffset);
  Serial.printf("Zero offset = %.2f counts\n", zeroOffset);
}

void calibrate(float knownmT) {
  float delta = readHallRaw(SAMPLES_PER_READING * 8) - zeroOffset;
  if (fabsf(delta) < 3.0f || knownmT == 0.0f) {
    Serial.println("Calibration failed: field too weak. Bring the magnet closer.");
    return;
  }
  mTPerCount = fabsf(knownmT / delta);
  prefs.putFloat("scale", mTPerCount);
  Serial.printf("Calibrated: %.4f mT per count (delta %.2f counts)\n", mTPerCount, delta);
}

void printSettings() {
  Serial.printf("Zero offset : %.2f counts\n", zeroOffset);
  Serial.printf("Sensitivity : %.4f mT/count\n", mTPerCount);
  Serial.printf("Threshold   : on %.2f mT, off %.2f mT\n", DETECT_ON_MT, DETECT_OFF_MT);
}

void handleSerial() {
  if (!Serial.available()) return;
  String line = Serial.readStringUntil('\n');
  line.trim();
  if (line.length() == 0) return;

  char cmd = tolower(line.charAt(0));
  if (cmd == 'z') {
    zeroSensor();
  } else if (cmd == 'c') {
    calibrate(line.substring(1).toFloat());
  } else if (cmd == 's') {
    printSettings();
  } else {
    Serial.println("Commands: z (zero), c <mT> (calibrate), s (settings)");
  }
}

// Formats the field with an SI prefix that fits the 16-char LCD line
void formatField(float mT, char *out, size_t len) {
  if (fabsf(mT) < 1.0f) {
    snprintf(out, len, "B=%+7.0f uT", mT * 1000.0f);
  } else {
    snprintf(out, len, "B=%+7.2f mT", mT);
  }
}

void setLeds(bool on) {
  digitalWrite(EXT_LED_PIN, on ? HIGH : LOW);
  digitalWrite(ONBOARD_LED_PIN, on ? HIGH : LOW);
}

void updateLed() {
  if (!fieldPresent) {
    ledState = false;
    setLeds(false);
    return;
  }
  float strength = constrain(fabsf(fieldmT) / BLINK_FULL_SCALE_MT, 0.0f, 1.0f);
  uint32_t period = BLINK_SLOW_MS - (uint32_t)(strength * (BLINK_SLOW_MS - BLINK_FAST_MS));
  uint32_t now = millis();
  if (now - lastToggleMs >= period / 2) {
    lastToggleMs = now;
    ledState = !ledState;
    setLeds(ledState);
  }
}

void updateLcd() {
  uint32_t now = millis();
  if (now - lastLcdMs < LCD_UPDATE_MS) return;
  lastLcdMs = now;

  char line[LCD_COLS + 1];
  formatField(fieldmT, line, sizeof(line));
  lcdLine(0, line);

  if (fieldPresent) {
    // Sign of the reading tells which pole faces the chip
    lcdLine(1, fieldmT >= 0 ? "MAGNET! Pole: +" : "MAGNET! Pole: -");
  } else {
    lcdLine(1, "No magnet");
  }

  Serial.printf("raw=%.1f  B=%.3f mT  %s\n", fieldmT / mTPerCount + zeroOffset,
                fieldmT, fieldPresent ? "DETECTED" : "-");
}

void setup() {
  Serial.begin(115200);
  pinMode(EXT_LED_PIN, OUTPUT);
  pinMode(ONBOARD_LED_PIN, OUTPUT);
  setLeds(false);

  Wire.begin(I2C_SDA, I2C_SCL);
  setupLcd();
  lcdLine(0, "ESP32 Hall Meter");
  lcdLine(1, "Unit: tesla (T)");
  delay(1500);

  prefs.begin("hallmeter", false);
  mTPerCount = prefs.getFloat("scale", DEFAULT_MT_PER_COUNT);

  // Re-zero on every boot so drift and the local background field are removed
  zeroSensor();
  printSettings();
  Serial.println("Commands: z (zero), c <mT> (calibrate), s (settings)");
}

void loop() {
  handleSerial();

  float raw = readHallRaw();
  float instantmT = (raw - zeroOffset) * mTPerCount;
  fieldmT = EMA_ALPHA * instantmT + (1.0f - EMA_ALPHA) * fieldmT;

  float magnitude = fabsf(fieldmT);
  if (!fieldPresent && magnitude >= DETECT_ON_MT) {
    fieldPresent = true;
  } else if (fieldPresent && magnitude <= DETECT_OFF_MT) {
    fieldPresent = false;
  }

  updateLed();
  updateLcd();
}
