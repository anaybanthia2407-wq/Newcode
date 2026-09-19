/*
  ESP32 DevKit V1 + MH-Sensor-Series Flame Sensor + SSD1306 OLED
  ----------------------------------------------------------------
  Reads an MH-series IR flame sensor module (digital + analog output)
  and shows live status ("FIRE DETECTED!" / "No Fire") plus the analog
  flame reading on a 128x64 I2C OLED. Optionally drives a buzzer while
  fire is detected.

  Wiring:
    Flame sensor -> ESP32 DevKit V1
      VCC -> 3V3
      GND -> GND
      DO  -> GPIO27   (digital output, LOW = flame detected)
      AO  -> GPIO34   (analog output, ADC1_CH6, lower = stronger flame)

    SSD1306 OLED (I2C) -> ESP32 DevKit V1
      VCC -> 3V3
      GND -> GND
      SDA -> GPIO21
      SCL -> GPIO22

    Buzzer (optional, active-HIGH) -> ESP32 DevKit V1
      +   -> GPIO25
      -   -> GND

  Required libraries (install via Arduino Library Manager):
    - Adafruit GFX Library
    - Adafruit SSD1306
*/

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ---- Flame sensor pins ----
#define FLAME_DO_PIN 27
#define FLAME_AO_PIN 34

// ---- Buzzer ----
#define BUZZER_PIN 25
#define BUZZER_ENABLED true

// ---- OLED settings ----
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Analog readings below this are treated as a strong flame signal even if
// the module's digital comparator (DO) hasn't tripped yet. Tune to your
// sensor/environment; lower = closer flame required.
const int ANALOG_ALERT_THRESHOLD = 1500;

void setup() {
  Serial.begin(115200);

  pinMode(FLAME_DO_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println("SSD1306 allocation failed");
    while (true) delay(1000);
  }

  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Fire Sensor Ready");
  display.display();
  delay(1000);
}

void loop() {
  int digitalReading = digitalRead(FLAME_DO_PIN); // LOW = flame detected
  int analogReading = analogRead(FLAME_AO_PIN);   // 0-4095, lower = stronger flame

  bool fireDetected = (digitalReading == LOW) || (analogReading < ANALOG_ALERT_THRESHOLD);

  digitalWrite(BUZZER_PIN, (BUZZER_ENABLED && fireDetected) ? HIGH : LOW);

  Serial.printf("DO=%d  AO=%d  Fire=%s\n",
                digitalReading, analogReading, fireDetected ? "YES" : "no");

  display.clearDisplay();

  display.setTextSize(2);
  display.setCursor(0, 0);
  display.println(fireDetected ? "FIRE!" : "No Fire");

  display.setTextSize(1);
  display.setCursor(0, 24);
  display.print("Analog: ");
  display.println(analogReading);

  display.setCursor(0, 36);
  display.print("Digital: ");
  display.println(digitalReading == LOW ? "TRIGGERED" : "clear");

  display.setCursor(0, 52);
  display.println(fireDetected ? "!! CHECK AREA !!" : "Status: normal");

  display.display();

  delay(300);
}
