/*
 * ESP32 Smart Street Light
 *
 * An LDR decides whether it is day or night. An IR obstacle sensor detects a
 * car/object passing underneath. At night the LED turns on while the object
 * is under the sensor and turns off once it has passed. During the day the
 * LED stays off no matter what the IR sensor sees. A 16x2 I2C LCD shows the
 * current mode, light level, detection state and LED state.
 *
 * Hardware: ESP32 DevKit V1, IR obstacle sensor module, LDR + 10k resistor,
 *           LED + 220R resistor, 16x2 LCD with I2C (PCF8574) backpack.
 *
 * No LCD library is needed: the small driver below talks to the I2C backpack
 * directly using Wire.h, so LiquidCrystal_I2C version conflicts don't matter.
 */

#include <Wire.h>

// ---------------- Pins ----------------
const int IR_PIN  = 35;  // IR sensor OUT (digital, input-only pin)
const int LDR_PIN = 32;  // LDR voltage divider (ADC1)
const int LED_PIN = 18;  // Street light LED (through 220R)
const int SDA_PIN = 21;  // LCD SDA
const int SCL_PIN = 22;  // LCD SCL

// ---------------- Settings ----------------
// IR sensor direction is detected automatically at startup: whatever the
// sensor outputs while NOTHING is in front of it is taken as "no object".
// Keep the area in front of the IR sensor clear while the ESP32 boots.
// Set IR_AUTO_DETECT to false to use IR_ACTIVE_LOW instead.
const bool IR_AUTO_DETECT = true;
const bool IR_ACTIVE_LOW = true;  // most modules pull OUT LOW on detection

// LDR reading is 0..4095. With a bare LDR (LDR to 3V3, 10k to GND)
// a BRIGHTER room gives a HIGHER reading. If you use an LDR module
// whose AO goes DOWN in bright light, set this to true.
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
const unsigned long LCD_RETRY_MS   = 3000;  // look for the LCD again if missing

// ---------------- Minimal I2C LCD driver (PCF8574 + HD44780) ----------------
// Backpack pin mapping: P0=RS, P1=RW, P2=EN, P3=backlight, P4..P7=D4..D7
const uint8_t LCD_RS = 0x01;
const uint8_t LCD_EN = 0x04;
const uint8_t LCD_BACKLIGHT = 0x08;

uint8_t lcdAddress = 0;  // 0 = no LCD found

bool i2cDevicePresent(uint8_t address) {
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
}

uint8_t findLcdAddress() {
  // Common addresses first, then the rest of the PCF8574 / PCF8574A ranges.
  const uint8_t candidates[] = {0x27, 0x3F, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26,
                                0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E};
  for (uint8_t address : candidates) {
    if (i2cDevicePresent(address)) return address;
  }
  return 0;
}

void lcdWriteByte(uint8_t data) {
  Wire.beginTransmission(lcdAddress);
  Wire.write(data | LCD_BACKLIGHT);
  Wire.endTransmission();
}

void lcdWriteNibble(uint8_t nibble, uint8_t mode) {
  uint8_t data = (nibble & 0xF0) | mode;
  lcdWriteByte(data | LCD_EN);
  delayMicroseconds(1);
  lcdWriteByte(data);
  delayMicroseconds(50);
}

void lcdSend(uint8_t value, uint8_t mode) {
  lcdWriteNibble(value & 0xF0, mode);
  lcdWriteNibble(value << 4, mode);
}

void lcdCommand(uint8_t command) {
  lcdSend(command, 0);
}

void lcdClear() {
  if (!lcdAddress) return;
  lcdCommand(0x01);
  delay(2);
}

void lcdSetCursor(uint8_t col, uint8_t row) {
  if (!lcdAddress) return;
  lcdCommand(0x80 | (col + (row ? 0x40 : 0x00)));
}

void lcdPrint(const char *text) {
  if (!lcdAddress) return;
  while (*text) {
    lcdSend(*text++, LCD_RS);
  }
}

bool lcdBegin() {
  lcdAddress = findLcdAddress();
  if (!lcdAddress) return false;

  // HD44780 power-on sequence to force 4-bit mode.
  delay(50);
  lcdWriteByte(0);
  delay(5);
  lcdWriteNibble(0x30, 0);
  delay(5);
  lcdWriteNibble(0x30, 0);
  delay(5);
  lcdWriteNibble(0x30, 0);
  delayMicroseconds(150);
  lcdWriteNibble(0x20, 0);

  lcdCommand(0x28);  // 4-bit, 2 lines, 5x8 font
  lcdCommand(0x0C);  // display on, cursor off, blink off
  lcdClear();
  lcdCommand(0x06);  // move cursor right after each character
  return true;
}

// ---------------- State ----------------
bool isNight = false;
bool objectPresent = false;
bool ledOn = false;
int lightLevel = 0;
unsigned long passCount = 0;
unsigned long lastSeenMs = 0;
unsigned long lastLdrMs = 0;
unsigned long lastLcdMs = 0;
unsigned long lastLogMs = 0;
unsigned long lastLcdRetryMs = 0;
int ldrRaw = 0;
int irIdleLevel = HIGH;  // IR pin level when nothing is in front of it

int readLightLevel() {
  // Average several samples to smooth out ADC noise.
  long sum = 0;
  for (int i = 0; i < 16; i++) {
    sum += analogRead(LDR_PIN);
  }
  ldrRaw = sum / 16;
  return LDR_INVERTED ? 4095 - ldrRaw : ldrRaw;
}

bool irDetects() {
  return digitalRead(IR_PIN) != irIdleLevel;
}

void showSplash() {
  lcdSetCursor(0, 0);
  lcdPrint("  Smart Street  ");
  lcdSetCursor(0, 1);
  lcdPrint("  Light System  ");
  delay(1500);
  lcdClear();
}

void retryLcd(unsigned long now) {
  if (lcdAddress || now - lastLcdRetryMs < LCD_RETRY_MS) return;
  lastLcdRetryMs = now;
  if (lcdBegin()) {
    Serial.printf(">> LCD connected at 0x%02X\n", lcdAddress);
    showSplash();
  }
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
  // Both lines are padded to exactly 16 characters so old text is overwritten.
  // "NIGHT Light:1234" / "Obj:YES LED:OFF "
  snprintf(line1, sizeof(line1), "%-5s Light:%-4d", isNight ? "NIGHT" : "DAY", lightLevel);
  snprintf(line2, sizeof(line2), "Obj:%-3s LED:%-4s",
           objectPresent ? "YES" : "NO", ledOn ? "ON" : "OFF");

  lcdSetCursor(0, 0);
  lcdPrint(line1);
  lcdSetCursor(0, 1);
  lcdPrint(line2);
}

void setup() {
  Serial.begin(115200);

  pinMode(IR_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  pinMode(LDR_PIN, INPUT);
  analogReadResolution(12);
  analogSetPinAttenuation(LDR_PIN, ADC_11db);  // full 0-3.3V range

  delay(500);
  Serial.println();
  Serial.println("===== Smart Street Light starting =====");

  Wire.begin(SDA_PIN, SCL_PIN);
  if (lcdBegin()) {
    Serial.printf("LCD found at 0x%02X\n", lcdAddress);
    showSplash();
  } else {
    // Keep running without the LCD so the street light still works.
    Serial.println("LCD NOT FOUND - check SDA=D21, SCL=D22, VCC=VIN(5V), GND");
  }

  // Learn what the IR sensor outputs when nothing is in front of it.
  if (IR_AUTO_DETECT) {
    int lowCount = 0;
    for (int i = 0; i < 50; i++) {
      if (digitalRead(IR_PIN) == LOW) lowCount++;
      delay(10);
    }
    irIdleLevel = lowCount > 25 ? LOW : HIGH;
    Serial.printf("IR idle level = %s (object detected when %s)\n",
                  irIdleLevel == HIGH ? "HIGH" : "LOW",
                  irIdleLevel == HIGH ? "LOW" : "HIGH");
  } else {
    irIdleLevel = IR_ACTIVE_LOW ? HIGH : LOW;
  }

  // Decide day/night straight away instead of waiting for the first sample.
  lightLevel = readLightLevel();
  isNight = lightLevel < NIGHT_THRESHOLD;
  Serial.printf("Start: light=%d mode=%s\n", lightLevel, isNight ? "NIGHT" : "DAY");
  if (ldrRaw == 0 || ldrRaw >= 4095) {
    Serial.println("WARNING: LDR reads 0 or 4095 - check LDR wiring to D32 "
                   "(module: use the AO pin, not DO)");
  }
  Serial.println("========================================");
}

void loop() {
  unsigned long now = millis();

  updateDayNight(now);
  updateObject(now);

  // The whole rule: light only at night, and only while an object is there.
  setLed(isNight && objectPresent);

  updateLcd(now);
  retryLcd(now);

  if (now - lastLogMs >= 1000) {
    lastLogMs = now;
    char lcdStatus[8];
    if (lcdAddress) {
      snprintf(lcdStatus, sizeof(lcdStatus), "0x%02X", lcdAddress);
    } else {
      snprintf(lcdStatus, sizeof(lcdStatus), "NONE");
    }
    Serial.printf("Light:%4d (raw %4d)  Mode:%-5s  IRpin:%s  Object:%-3s  LED:%-3s  "
                  "Passed:%lu  LCD:%s\n",
                  lightLevel, ldrRaw, isNight ? "NIGHT" : "DAY",
                  digitalRead(IR_PIN) == HIGH ? "HIGH" : "LOW",
                  objectPresent ? "YES" : "NO", ledOn ? "ON" : "OFF", passCount, lcdStatus);
  }
}
