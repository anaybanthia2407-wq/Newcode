/*
 * ESP32 Smart Street Light
 *
 * An LDR decides whether it is day or night. An IR obstacle sensor detects a
 * car/object passing underneath. At night the LED turns on while the object
 * is under the sensor and turns off once it has passed. During the day the
 * LED stays off no matter what the IR sensor sees. A 16x2 I2C LCD shows the
 * current mode, light level, detection state, LED state and a pass counter.
 *
 * Hardware: ESP32 DevKit V1, IR obstacle sensor module, LDR + 10k resistor,
 *           LED + 220R resistor, 16x2 LCD with I2C (PCF8574) backpack.
 */

#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ---------------- Pins ----------------
const int IR_PIN  = 27;  // IR sensor OUT (digital)
const int LDR_PIN = 34;  // LDR voltage divider (ADC1, input-only pin)
const int LED_PIN = 26;  // Street light LED (through 220R)
// LCD uses the default I2C pins: SDA = GPIO21, SCL = GPIO22

// ---------------- Settings ----------------
const uint8_t LCD_ADDRESS = 0x27;  // Try 0x3F if the LCD stays blank

// Most IR obstacle modules pull OUT LOW when something is detected.
const bool IR_ACTIVE_LOW = true;

// LDR reading is 0..4095. With the wiring in the README (LDR to 3V3,
// 10k to GND) a BRIGHTER room gives a HIGHER reading. If you use an LDR
// module whose AO goes DOWN in bright light, set this to true.
const bool LDR_INVERTED = false;

// Day/night thresholds with hysteresis so the mode doesn't flicker at dusk.
// Calibrate using the "Light:" value printed on Serial Monitor / LCD.
const int NIGHT_THRESHOLD = 1200;  // go to NIGHT when light falls below this
const int DAY_THRESHOLD   = 1600;  // go back to DAY when light rises above this

// Object must be continuously absent for this long before the LED turns off.
// Filters out sensor flicker between the front and back of a car.
const unsigned long OBJECT_CLEAR_MS = 300;

const unsigned long LDR_SAMPLE_MS  = 100;
const unsigned long LCD_REFRESH_MS = 250;

// ---------------- State ----------------
LiquidCrystal_I2C lcd(LCD_ADDRESS, 16, 2);

bool isNight = false;
bool objectPresent = false;
bool ledOn = false;
int lightLevel = 0;
unsigned long passCount = 0;
unsigned long lastSeenMs = 0;
unsigned long lastLdrMs = 0;
unsigned long lastLcdMs = 0;

int readLightLevel() {
  // Average several samples to smooth out ADC noise.
  long sum = 0;
  for (int i = 0; i < 16; i++) {
    sum += analogRead(LDR_PIN);
  }
  int value = sum / 16;
  return LDR_INVERTED ? 4095 - value : value;
}

bool irDetects() {
  int raw = digitalRead(IR_PIN);
  return IR_ACTIVE_LOW ? raw == LOW : raw == HIGH;
}

void updateDayNight(unsigned long now) {
  if (now - lastLdrMs < LDR_SAMPLE_MS) return;
  lastLdrMs = now;

  lightLevel = readLightLevel();
  if (!isNight && lightLevel < NIGHT_THRESHOLD) {
    isNight = true;
    Serial.println(">> Night detected");
  } else if (isNight && lightLevel > DAY_THRESHOLD) {
    isNight = false;
    Serial.println(">> Day detected");
  }
}

void updateObject(unsigned long now) {
  if (irDetects()) {
    lastSeenMs = now;
    if (!objectPresent) {
      objectPresent = true;
      Serial.println(">> Object arrived");
    }
  } else if (objectPresent && now - lastSeenMs >= OBJECT_CLEAR_MS) {
    objectPresent = false;
    passCount++;
    Serial.printf(">> Object passed (total %lu)\n", passCount);
  }
}

void setLed(bool on) {
  if (on == ledOn) return;
  ledOn = on;
  digitalWrite(LED_PIN, on ? HIGH : LOW);
  Serial.println(on ? ">> LED ON" : ">> LED OFF");
}

void updateLcd(unsigned long now) {
  if (now - lastLcdMs < LCD_REFRESH_MS) return;
  lastLcdMs = now;

  char line1[17];
  char line2[17];
  // "NIGHT Light:1234" / "Obj:YES LED:OFF "
  snprintf(line1, sizeof(line1), "%-5s Light:%-4d", isNight ? "NIGHT" : "DAY", lightLevel);
  snprintf(line2, sizeof(line2), "Obj:%-3s LED:%-4s",
           objectPresent ? "YES" : "NO", ledOn ? "ON" : "OFF");

  lcd.setCursor(0, 0);
  lcd.print(line1);
  lcd.setCursor(0, 1);
  lcd.print(line2);
}

void setup() {
  Serial.begin(115200);

  pinMode(IR_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  analogReadResolution(12);
  analogSetPinAttenuation(LDR_PIN, ADC_11db);  // full 0-3.3V range

  Wire.begin(21, 22);
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print(" Smart Street  ");
  lcd.setCursor(0, 1);
  lcd.print("  Light System  ");
  delay(1500);
  lcd.clear();

  // Decide day/night straight away instead of waiting for the first sample.
  lightLevel = readLightLevel();
  isNight = lightLevel < NIGHT_THRESHOLD;
  Serial.printf("Start: light=%d mode=%s\n", lightLevel, isNight ? "NIGHT" : "DAY");
}

void loop() {
  unsigned long now = millis();

  updateDayNight(now);
  updateObject(now);

  // The whole rule: light only at night, and only while an object is there.
  setLed(isNight && objectPresent);

  updateLcd(now);

  static unsigned long lastLogMs = 0;
  if (now - lastLogMs >= 1000) {
    lastLogMs = now;
    Serial.printf("Light:%4d  Mode:%-5s  Object:%-3s  LED:%-3s  Passed:%lu\n",
                  lightLevel, isNight ? "NIGHT" : "DAY",
                  objectPresent ? "YES" : "NO", ledOn ? "ON" : "OFF", passCount);
  }
}
