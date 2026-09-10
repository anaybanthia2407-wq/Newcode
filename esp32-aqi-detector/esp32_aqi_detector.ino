/*
  ESP32 DevKit V1 + MQ135 + SSD1306 OLED -- Air Quality Detector
  ------------------------------------------------------------------
  Reads the MQ135 gas sensor, converts its analog output to an
  approximate CO2-equivalent PPM reading, classifies it into an air
  quality category, and shows both on a 128x64 I2C OLED.

  Wiring (default: MQ135 powered at 3.3V, no divider needed):
    MQ135 VCC  -> 3V3
    MQ135 GND  -> GND
    MQ135 AOUT -> GPIO34   (ADC1_CH6, input-only)

    OLED VCC -> 3V3
    OLED GND -> GND
    OLED SDA -> GPIO21
    OLED SCL -> GPIO22

  If you power the MQ135 from 5V instead (see README), add a resistor
  divider on AOUT before it reaches GPIO34 and update SENSOR_VCC /
  DIVIDER_RATIO below accordingly -- do not feed 5V straight into an
  ESP32 ADC pin.

  Required libraries (Arduino Library Manager):
    - Adafruit GFX Library
    - Adafruit SSD1306

  Notes:
    - The MQ135 is a broad-spectrum gas sensor (CO2, NH3, benzene,
      alcohol, smoke...), not a true PM2.5/PM10 particulate sensor.
      The PPM value here is a CO2-equivalent estimate and the "AQI"
      categories are a simplified indoor-air-quality scale, not the
      official EPA AQI.
    - The sensor needs to warm up before readings are meaningful. This
      sketch preheats for WARMUP_MS, then self-calibrates its clean-air
      baseline (RZERO) -- keep it in fresh air during that step. Per
      the datasheet, absolute accuracy improves after a 24-48h burn-in;
      relative changes (better/worse) are meaningful much sooner.
*/

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ---- Pin config ----
#define MQ135_PIN 34 // ADC1 input-only pin

// ---- OLED config ----
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ---- ADC / sensor supply ----
const float ADC_VREF = 3.3;       // ESP32 ADC reference voltage
const int ADC_RESOLUTION = 4095;  // 12-bit ADC
const float SENSOR_VCC = 3.3;     // Voltage actually applied to the MQ135's VCC pin
const float DIVIDER_RATIO = 1.0;  // Set to (R1+R2)/R2 if you add a divider for 5V operation
const float RLOAD = 22.0;         // kOhm, nominal load resistor (cancels out of the PPM math)

// ---- MQ135 curve constants (CO2, from the sensor's datasheet curve fit) ----
const float PARA = 116.6020682;
const float PARB = 2.769034857;
const float ATMOSPHERIC_CO2 = 397.13; // ppm, assumed clean-air baseline used for calibration

// ---- Timing ----
const unsigned long WARMUP_MS = 20000;     // sensor preheat before the first reading
const unsigned long CALIBRATION_MS = 5000; // clean-air sampling window for RZERO
const unsigned long READ_INTERVAL_MS = 1000;
const float EMA_ALPHA = 0.2; // smoothing factor for the displayed PPM

float rZero = 76.63; // fallback default, overwritten by calibrate()
float smoothedPPM = -1;

float readResistance() {
  int raw = analogRead(MQ135_PIN);
  float vAtPin = (raw / (float)ADC_RESOLUTION) * ADC_VREF;
  float vOut = vAtPin * DIVIDER_RATIO; // undo any divider to get the sensor's true output voltage
  if (vOut < 0.01) vOut = 0.01;        // guard against divide-by-zero in clean/no-signal conditions
  return ((SENSOR_VCC / vOut) - 1.0) * RLOAD;
}

float resistanceToPPM(float resistance) {
  return PARA * pow(resistance / rZero, -PARB);
}

const char *classifyAQI(float ppm) {
  if (ppm < 700) return "GOOD";
  if (ppm < 1000) return "MODERATE";
  if (ppm < 2000) return "POOR";
  if (ppm < 5000) return "UNHEALTHY";
  return "SEVERE";
}

void showWarmup(unsigned long remainingMs) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("MQ135 warming up...");
  display.setCursor(0, 20);
  display.print(remainingMs / 1000);
  display.println("s remaining");
  display.display();
}

void calibrate() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("Calibrating...");
  display.println("Keep sensor in");
  display.println("clean/fresh air");
  display.display();

  double total = 0;
  int samples = 0;
  unsigned long start = millis();
  while (millis() - start < CALIBRATION_MS) {
    total += readResistance();
    samples++;
    delay(50);
  }
  float avgResistance = total / samples;
  rZero = avgResistance * pow(ATMOSPHERIC_CO2 / PARA, 1.0 / PARB);
}

void drawReading(float ppm, const char *category) {
  display.clearDisplay();

  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("Air Quality Monitor");
  display.drawLine(0, 10, SCREEN_WIDTH - 1, 10, SSD1306_WHITE);

  char ppmStr[16];
  snprintf(ppmStr, sizeof(ppmStr), "%.0f ppm", ppm);
  display.setTextSize(2);
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(ppmStr, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, 20);
  display.println(ppmStr);

  display.getTextBounds(category, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, 44);
  display.println(category);

  // Simple quality bar: more fill = worse air (capped at 5000 ppm)
  float clamped = ppm;
  if (clamped < 0) clamped = 0;
  if (clamped > 5000) clamped = 5000;
  int barWidth = (int)((clamped / 5000.0) * SCREEN_WIDTH);
  display.drawRect(0, 60, SCREEN_WIDTH, 4, SSD1306_WHITE);
  display.fillRect(0, 60, barWidth, 4, SSD1306_WHITE);

  display.display();
}

void setup() {
  Serial.begin(115200);
  analogReadResolution(12);
  analogSetPinAttenuation(MQ135_PIN, ADC_11db); // full 0-3.3V input range

  Wire.begin(21, 22); // SDA, SCL -- explicit so it doesn't depend on library defaults

  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    // Nothing will appear on the OLED if this fails. Check this Serial
    // message first: it means the ESP32 got no response at SCREEN_ADDRESS.
    // Run i2c_scanner.ino to confirm the real address and that the bus
    // sees the display at all (wiring/power vs. address mismatch).
    Serial.println("SSD1306 allocation failed -- check wiring/address, see i2c_scanner.ino");
    while (true) delay(1000);
  }
  display.setTextColor(SSD1306_WHITE);

  unsigned long start = millis();
  unsigned long lastUpdate = 0;
  while (millis() - start < WARMUP_MS) {
    if (millis() - lastUpdate > 500) {
      showWarmup(WARMUP_MS - (millis() - start));
      lastUpdate = millis();
    }
    delay(10);
  }

  calibrate();
}

void loop() {
  static unsigned long lastRead = 0;
  if (millis() - lastRead < READ_INTERVAL_MS) return;
  lastRead = millis();

  float resistance = readResistance();
  float ppm = resistanceToPPM(resistance);

  smoothedPPM = (smoothedPPM < 0) ? ppm : (EMA_ALPHA * ppm + (1 - EMA_ALPHA) * smoothedPPM);

  const char *category = classifyAQI(smoothedPPM);
  drawReading(smoothedPPM, category);

  Serial.print("PPM: ");
  Serial.print(smoothedPPM);
  Serial.print(" | ");
  Serial.println(category);
}
