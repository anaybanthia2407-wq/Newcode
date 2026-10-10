/*
 * ESP32 Hall-Effect Magnetic Field Detector
 * Board : ESP32 DevKit V1 (ESP32-WROOM-32)
 *
 * Uses the ESP32's built-in Hall-effect sensor to detect a nearby magnetic
 * field. An external LED (plus the on-board LED) blinks while a field is
 * present (faster for a stronger field), a second LED stays solidly ON while
 * a field is detected, and a 128x64 SSD1306 OLED shows the magnetic flux
 * density in its SI unit, the tesla (displayed in mT).
 *
 * Serial Plotter (Tools > Serial Plotter, 115200 baud) graphs the field
 * strength and a "Magnet" line that goes high while a magnet is detected.
 *
 * IMPORTANT: hallRead() only exists in Arduino-ESP32 core 1.x / 2.x.
 * It was removed in core 3.x, so install "esp32 by Espressif" version 2.0.17
 * in the Boards Manager.
 *
 * Wiring (SSD1306 I2C OLED -> ESP32 DevKit V1):
 *   VCC -> 3V3
 *   GND -> GND
 *   SDA -> GPIO21 (D21)
 *   SCL -> GPIO22 (D22)
 *
 * External LED wiring (each LED with its own 220 ohm resistor):
 *   Blinking LED : GPIO4  (D4)  -> 220 ohm -> LED long leg (+), short leg (-) -> GND
 *   Detect LED   : GPIO18 (D18) -> 220 ohm -> LED long leg (+), short leg (-) -> GND
 *
 * The Hall sensor is read internally through GPIO36 (VP) and GPIO39 (VN):
 * leave both pins unconnected.
 *
 * Required libraries (install via Arduino Library Manager):
 *   - Adafruit GFX Library
 *   - Adafruit SSD1306
 *
 * Serial commands (115200 baud, newline terminated):
 *   z          re-zero: measure the background field with no magnet nearby
 *   c <mT>     calibrate: hold a magnet of known field <mT> at the sensor,
 *              then send e.g. "c 25" to compute the counts-to-mT factor
 *   s          print current settings
 * Calibration is stored in flash and survives a reboot.
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
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Preferences.h>

// ---------------- Hardware ----------------
const uint8_t EXT_LED_PIN     = 4;  // External LED, blinks faster for a stronger field
const uint8_t DETECT_LED_PIN  = 18; // External LED, solid ON while a magnet is detected
const uint8_t ONBOARD_LED_PIN = 2;  // On-board blue LED on DevKit V1
const uint8_t I2C_SDA = 21;
const uint8_t I2C_SCL = 22;

// ---------------- OLED ----------------
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C   // Found with the I2C scanner

// ---------------- Measurement ----------------
const int   SAMPLES_PER_BLOCK   = 32;    // hallRead() samples averaged per block
const int   BLOCKS_PER_READING  = 9;     // median of block averages rejects spikes
const int   ZERO_SAMPLES        = 40;    // readings averaged for the zero offset
const float EMA_ALPHA           = 0.15f; // smoothing (0..1, lower = steadier but slower)
const float DEADBAND_MT         = 0.5f;  // readings smaller than this are shown as 0

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

const uint32_t DISPLAY_UPDATE_MS = 250;
const uint32_t PLOT_UPDATE_MS    = 50;

// Height of the "Magnet" line in the Serial Plotter while a magnet is detected
const float PLOT_MAGNET_HIGH_MT = 10.0f;

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
Preferences prefs;

float zeroOffset   = 0.0f;   // background reading in raw counts
float mTPerCount   = DEFAULT_MT_PER_COUNT;
float fieldmT      = 0.0f;   // smoothed field, signed
bool  fieldPresent = false;

bool     ledState      = false;
uint32_t lastToggleMs  = 0;
uint32_t lastDisplayMs = 0;
uint32_t lastPlotMs    = 0;

// Shows up to two lines of size-1 text, used for status messages
void showMessage(const char *line1, const char *line2) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 20);
  display.println(line1);
  display.println(line2);
  display.display();
}

// Average of one block of raw Hall samples
float readHallBlock() {
  long sum = 0;
  for (int i = 0; i < SAMPLES_PER_BLOCK; i++) {
    sum += hallRead();
  }
  return (float)sum / SAMPLES_PER_BLOCK;
}

// Median of several block averages: steadier than a plain average because
// occasional noise spikes are thrown away instead of averaged in
float readHallRaw() {
  float blocks[BLOCKS_PER_READING];
  for (int i = 0; i < BLOCKS_PER_READING; i++) {
    float v = readHallBlock();
    int j = i;
    while (j > 0 && blocks[j - 1] > v) {   // insertion sort
      blocks[j] = blocks[j - 1];
      j--;
    }
    blocks[j] = v;
  }
  return blocks[BLOCKS_PER_READING / 2];
}

// Field with small noise around zero removed
float shownField() {
  return fabsf(fieldmT) < DEADBAND_MT ? 0.0f : fieldmT;
}

void zeroSensor() {
  showMessage("Zeroing sensor...", "Keep magnet away");
  Serial.println("Zeroing: keep magnets away from the board...");

  float sum = 0;
  for (int i = 0; i < ZERO_SAMPLES; i++) {
    sum += readHallRaw();
    delay(10);
  }
  zeroOffset = sum / ZERO_SAMPLES;
  fieldmT = 0.0f;
  Serial.printf("Zero offset = %.2f counts\n", zeroOffset);
}

void calibrate(float knownmT) {
  float sum = 0;
  for (int i = 0; i < 8; i++) {
    sum += readHallRaw();
  }
  float delta = sum / 8 - zeroOffset;
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

void setLeds(bool on) {
  digitalWrite(EXT_LED_PIN, on ? HIGH : LOW);
  digitalWrite(ONBOARD_LED_PIN, on ? HIGH : LOW);
}

void updateLed() {
  digitalWrite(DETECT_LED_PIN, fieldPresent ? HIGH : LOW);

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

void updateDisplay() {
  uint32_t now = millis();
  if (now - lastDisplayMs < DISPLAY_UPDATE_MS) return;
  lastDisplayMs = now;

  display.clearDisplay();

  // Title
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("Magnetic Field (B)");

  // Value in large text
  float shown = shownField();
  char value[12];
  snprintf(value, sizeof(value), "%+.1f", shown);
  display.setTextSize(2);
  display.setCursor(0, 16);
  display.print(value);
  display.print(" mT");

  // Strength bar
  int barWidth = (int)(constrain(fabsf(shown) / BLINK_FULL_SCALE_MT, 0.0f, 1.0f) * (SCREEN_WIDTH - 2));
  display.drawRect(0, 38, SCREEN_WIDTH, 8, SSD1306_WHITE);
  display.fillRect(1, 39, barWidth, 6, SSD1306_WHITE);

  // Status
  display.setTextSize(1);
  display.setCursor(0, 54);
  if (fieldPresent) {
    // Sign of the reading tells which pole faces the chip
    display.print(fieldmT >= 0 ? "MAGNET!  Pole: +" : "MAGNET!  Pole: -");
  } else {
    display.print("No magnet");
  }

  display.display();
}

// One line per update in "label:value" form for the Arduino Serial Plotter.
// "Magnet" jumps high while a magnet is detected and stays at 0 otherwise.
void updatePlot() {
  uint32_t now = millis();
  if (now - lastPlotMs < PLOT_UPDATE_MS) return;
  lastPlotMs = now;

  Serial.printf("Field_mT:%.2f,Magnet:%.1f,Threshold:%.1f\n",
                fabsf(shownField()), fieldPresent ? PLOT_MAGNET_HIGH_MT : 0.0f,
                DETECT_ON_MT);
}

void setup() {
  Serial.begin(115200);
  pinMode(EXT_LED_PIN, OUTPUT);
  pinMode(DETECT_LED_PIN, OUTPUT);
  pinMode(ONBOARD_LED_PIN, OUTPUT);
  setLeds(false);
  digitalWrite(DETECT_LED_PIN, LOW);

  Wire.begin(I2C_SDA, I2C_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println("SSD1306 OLED not found! Check wiring and SCREEN_ADDRESS.");
  }
  display.setTextColor(SSD1306_WHITE);
  showMessage("ESP32 Hall Meter", "Unit: tesla (T)");
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
  updateDisplay();
  updatePlot();
}
