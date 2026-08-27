/*
  Arduino Uno - Water / Electrical Leak Detector
  -----------------------------------------------
  Two bare jumper wires act as sensing probes. When they are bridged
  by water (or a live/leaking conductive path in water), the analog
  reading on the sense pin rises above a threshold. On detection, an
  SSD1306 OLED shows a warning, an LED blinks, and a buzzer sounds.

  Wiring:
    Water sensor probes (bare jumper wire ends, tips ~1cm apart,
    both dipped in/near the water being monitored):
      Probe A (drive)  -> Arduino 5V
      Probe B (sense)  -> Arduino A0  AND  one leg of a 10k ohm
                           resistor, whose other leg goes to GND
                           (this pulls A0 LOW when dry)

    SSD1306 OLED (I2C, 128x64):
      VCC -> 5V
      GND -> GND
      SDA -> A4
      SCL -> A5

    LED:
      Anode (+)  -> D8 (through a ~220 ohm resistor)
      Cathode(-) -> GND

    Buzzer (active buzzer):
      + -> D7
      - -> GND

  Required libraries (install via Arduino Library Manager):
    - Adafruit GFX Library
    - Adafruit SSD1306
*/

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ---- Pin assignments ----
const uint8_t WATER_SENSE_PIN = A0;
const uint8_t LED_PIN = 8;
const uint8_t BUZZER_PIN = 7;

// ---- Detection tuning ----
// Raise this if the sensor triggers on humidity/noise alone,
// lower it if it fails to trigger on light water contact.
const int WATER_THRESHOLD = 100;
const unsigned long BLINK_INTERVAL_MS = 300;
const unsigned long BUZZ_INTERVAL_MS = 300;

// ---- OLED settings ----
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

bool ledState = false;
bool buzzerState = false;
unsigned long lastBlinkTime = 0;
unsigned long lastBuzzTime = 0;

void showSafeScreen(int reading) {
  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(0, 0);
  display.println("SAFE");
  display.setTextSize(1);
  display.setCursor(0, 24);
  display.println("No water detected");
  display.setCursor(0, 40);
  display.print("Sensor: ");
  display.println(reading);
  display.display();
}

void showAlertScreen(int reading) {
  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(0, 0);
  display.println("DANGER!");
  display.setTextSize(1);
  display.setCursor(0, 24);
  display.println("Water/leak detected!");
  display.setCursor(0, 40);
  display.print("Sensor: ");
  display.println(reading);
  display.display();
}

void setup() {
  Serial.begin(9600);

  pinMode(WATER_SENSE_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  digitalWrite(BUZZER_PIN, LOW);

  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println("SSD1306 allocation failed");
    while (true) delay(1000);
  }

  display.setTextColor(SSD1306_WHITE);
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("Water Leak Detector");
  display.println("Initializing...");
  display.display();
  delay(1500);
}

void loop() {
  int reading = analogRead(WATER_SENSE_PIN);
  bool waterDetected = reading > WATER_THRESHOLD;

  if (waterDetected) {
    showAlertScreen(reading);

    unsigned long now = millis();
    if (now - lastBlinkTime >= BLINK_INTERVAL_MS) {
      lastBlinkTime = now;
      ledState = !ledState;
      digitalWrite(LED_PIN, ledState ? HIGH : LOW);
    }
    if (now - lastBuzzTime >= BUZZ_INTERVAL_MS) {
      lastBuzzTime = now;
      buzzerState = !buzzerState;
      digitalWrite(BUZZER_PIN, buzzerState ? HIGH : LOW);
    }
  } else {
    showSafeScreen(reading);
    digitalWrite(LED_PIN, LOW);
    digitalWrite(BUZZER_PIN, LOW);
    ledState = false;
    buzzerState = false;
  }

  delay(50);
}
