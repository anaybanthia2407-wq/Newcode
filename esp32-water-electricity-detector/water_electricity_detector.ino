/*
  ESP32 DevKit V1 - Water / Electrical Leak Detector with Telegram Alerts
  -------------------------------------------------------------------------
  Two bare jumper wires act as sensing probes. When they are bridged by
  water (or a live/leaking conductive path in water), the analog reading
  on the sense pin drops below a threshold. On detection, an SSD1306 OLED
  shows a warning, an LED blinks, a buzzer sounds, and a message is sent
  to a Telegram chat via a bot.

  Wiring:
    Water sensor probes (bare jumper wire ends, tips ~1cm apart, both
    dipped in/near the water being monitored) -- no external pull
    resistor needed, the ESP32's internal pull-up is used instead
    (enabled in code via INPUT_PULLUP). Only GPIO32/33 among the ADC1
    pins support internal pull resistors (GPIO34-39 do not), hence
    GPIO32 is used here:
      Probe A (drive) -> ESP32 GND
      Probe B (sense) -> ESP32 GPIO32
                          (internal pull-up holds GPIO32 HIGH when dry)

    SSD1306 OLED (I2C, 128x64):
      VCC -> 3V3
      GND -> GND
      SDA -> GPIO21
      SCL -> GPIO22

    LED:
      Anode (+)  -> GPIO25 (through a ~220 ohm resistor)
      Cathode(-) -> GND

    Buzzer (active buzzer):
      + -> GPIO26
      - -> GND

  Required libraries (install via Arduino Library Manager):
    - Adafruit GFX Library
    - Adafruit SSD1306
    (WiFi, WiFiClientSecure, and HTTPClient ship with the ESP32 board package)

  Telegram bot setup:
    1. Message @BotFather on Telegram, send /newbot, and follow the
       prompts to get a bot token (looks like 123456789:AA...).
    2. Message your new bot once (anything) so it can see your chat.
    3. Visit https://api.telegram.org/bot<YOUR_TOKEN>/getUpdates in a
       browser and read the "chat":{"id": ...} value from the JSON —
       that's your TELEGRAM_CHAT_ID.
    4. Fill in TELEGRAM_BOT_TOKEN and TELEGRAM_CHAT_ID below.
*/

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

// ---- Wi-Fi credentials ----
const char *WIFI_SSID = "YOUR_WIFI_SSID";
const char *WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// ---- Telegram bot settings ----
const char *TELEGRAM_BOT_TOKEN = "YOUR_BOT_TOKEN";
const char *TELEGRAM_CHAT_ID = "YOUR_CHAT_ID";

// ---- Pin assignments ----
const uint8_t WATER_SENSE_PIN = 32;
const uint8_t LED_PIN = 25;
const uint8_t BUZZER_PIN = 26;

// ---- Detection tuning ----
// ESP32 ADC is 12-bit (0-4095). Dry reading sits near 4095 (internal
// pull-up). Water pulls it down. Lower this if the sensor triggers on
// humidity/noise alone, raise it if it fails to trigger on light contact.
const int WATER_THRESHOLD = 3500;
const unsigned long BLINK_INTERVAL_MS = 300;
const unsigned long BUZZ_INTERVAL_MS = 300;

// How often to re-send the Telegram alert while water is still detected.
const unsigned long ALERT_REPEAT_INTERVAL_MS = 60000;

// Give up on Wi-Fi after this long and continue offline (local OLED/LED/
// buzzer alerts still work; Telegram alerts are skipped until reconnected).
const unsigned long WIFI_CONNECT_TIMEOUT_MS = 15000;

// ---- OLED settings ----
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3D

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

bool ledState = false;
bool buzzerState = false;
bool wasWaterDetected = false;
unsigned long lastBlinkTime = 0;
unsigned long lastBuzzTime = 0;
unsigned long lastAlertTime = 0;

String urlEncode(const String &text) {
  String encoded = "";
  const char *hex = "0123456789ABCDEF";
  for (size_t i = 0; i < text.length(); i++) {
    char c = text[i];
    if (isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.' || c == '~') {
      encoded += c;
    } else if (c == ' ') {
      encoded += "%20";
    } else {
      encoded += '%';
      encoded += hex[(c >> 4) & 0xF];
      encoded += hex[c & 0xF];
    }
  }
  return encoded;
}

bool sendTelegramMessage(const String &message) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Telegram alert skipped: Wi-Fi not connected");
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();  // skip TLS cert validation (see README for caveats)

  HTTPClient https;
  String url = "https://api.telegram.org/bot" + String(TELEGRAM_BOT_TOKEN) +
               "/sendMessage?chat_id=" + String(TELEGRAM_CHAT_ID) +
               "&text=" + urlEncode(message);

  bool ok = false;
  if (https.begin(client, url)) {
    int httpCode = https.GET();
    ok = (httpCode == 200);
    if (!ok) {
      Serial.printf("Telegram send failed, HTTP code: %d\n", httpCode);
    }
    https.end();
  } else {
    Serial.println("Telegram send failed: could not begin HTTPS connection");
  }
  return ok;
}

void connectWiFi() {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Connecting to");
  display.println(WIFI_SSID);
  display.display();

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < WIFI_CONNECT_TIMEOUT_MS) {
    delay(300);
    display.print(".");
    display.display();
  }

  display.clearDisplay();
  display.setCursor(0, 0);
  if (WiFi.status() == WL_CONNECTED) {
    display.println("Wi-Fi connected!");
    display.println(WiFi.localIP());
  } else {
    display.println("Wi-Fi connect failed.");
    display.println("Running offline");
    display.println("(no Telegram alerts).");
    Serial.println("Wi-Fi connect timed out, continuing offline");
  }
  display.display();
  delay(1500);
}

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
  display.setCursor(0, 36);
  display.println("Telegram alert sent");
  display.setCursor(0, 48);
  display.print("Sensor: ");
  display.println(reading);
  display.display();
}

void setup() {
  Serial.begin(115200);

  pinMode(WATER_SENSE_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  digitalWrite(BUZZER_PIN, LOW);

  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println("SSD1306 allocation failed");
    while (true) delay(1000);
  }

  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.clearDisplay();
  display.println("Water Leak Detector");
  display.println("Initializing...");
  display.display();
  delay(1000);

  connectWiFi();
}

void loop() {
  int reading = analogRead(WATER_SENSE_PIN);
  bool waterDetected = reading < WATER_THRESHOLD;
  unsigned long now = millis();

  if (waterDetected) {
    showAlertScreen(reading);

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

    bool justTriggered = !wasWaterDetected;
    bool repeatDue = now - lastAlertTime >= ALERT_REPEAT_INTERVAL_MS;
    if (justTriggered || repeatDue) {
      lastAlertTime = now;
      sendTelegramMessage("\xE2\x9A\xA0\xEF\xB8\x8F Water/electrical leak detected! Sensor reading: " + String(reading));
    }
  } else {
    showSafeScreen(reading);
    digitalWrite(LED_PIN, LOW);
    digitalWrite(BUZZER_PIN, LOW);
    ledState = false;
    buzzerState = false;

    if (wasWaterDetected) {
      sendTelegramMessage("\xE2\x9C\x85 Water/leak alert cleared. Sensor is back to normal.");
    }
  }

  wasWaterDetected = waterDetected;
  delay(50);
}
