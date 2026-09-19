/*
  ESP32 DevKit V1 + MH-Sensor-Series Flame Sensor + SSD1306 OLED + Telegram
  ---------------------------------------------------------------------------
  Reads an MH-series IR flame sensor module (digital + analog output) and
  shows live status ("FIRE!" / "No Fire") plus the analog flame reading on a
  128x64 I2C OLED. Drives a buzzer while fire is detected, and sends a
  Telegram message through a bot as soon as a flame is detected (re-sent
  periodically while the flame persists).

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

    Alert LED (active-HIGH, with current-limiting resistor) -> ESP32 DevKit V1
      +   -> GPIO26
      -   -> GND

  Required libraries (install via Arduino Library Manager):
    - Adafruit GFX Library
    - Adafruit SSD1306
  (WiFi, WiFiClientSecure and HTTPClient ship with the ESP32 board package.)

  Telegram bot setup:
    1. Message @BotFather on Telegram, send /newbot, and copy the bot token.
    2. Message your new bot once (anything), then visit
       https://api.telegram.org/bot<TOKEN>/getUpdates in a browser and read
       "chat":{"id": ...} from the JSON to get your chat ID.
    3. Fill in TELEGRAM_BOT_TOKEN and TELEGRAM_CHAT_ID below.
*/

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ctype.h>

// ---- Wi-Fi credentials ----
const char *WIFI_SSID = "YOUR_WIFI_SSID";
const char *WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// ---- Telegram bot settings ----
const char *TELEGRAM_BOT_TOKEN = "YOUR_TELEGRAM_BOT_TOKEN";
const char *TELEGRAM_CHAT_ID = "YOUR_TELEGRAM_CHAT_ID";

// Minimum time between repeated Telegram alerts while the flame is still
// detected, so the bot doesn't spam a message every loop iteration.
const unsigned long TELEGRAM_RESEND_INTERVAL_MS = 60000;

// How often to retry connecting Wi-Fi if it's down (initial failure or a
// later drop), so a lost connection doesn't disable alerts permanently.
const unsigned long WIFI_RETRY_INTERVAL_MS = 30000;

// ---- Flame sensor pins ----
#define FLAME_DO_PIN 27
#define FLAME_AO_PIN 34

// ---- Buzzer ----
#define BUZZER_PIN 25
#define BUZZER_ENABLED true

// ---- Alert LED ----
#define LED_PIN 26
const unsigned long LED_FLASH_INTERVAL_MS = 300;

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

bool lastFireState = false;
unsigned long lastTelegramSendMs = 0;
unsigned long lastWifiRetryMs = 0;
String lastTelegramStatus = "not sent yet";

bool ledState = false;
unsigned long lastLedToggleMs = 0;

// Flashes LED_PIN on/off every LED_FLASH_INTERVAL_MS while fireDetected is
// true; keeps it off otherwise. Uses millis() so it doesn't block the loop.
void updateAlertLed(bool fireDetected) {
  if (!fireDetected) {
    ledState = false;
    digitalWrite(LED_PIN, LOW);
    return;
  }

  if (millis() - lastLedToggleMs >= LED_FLASH_INTERVAL_MS) {
    ledState = !ledState;
    digitalWrite(LED_PIN, ledState ? HIGH : LOW);
    lastLedToggleMs = millis();
  }
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
  while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 15000) {
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
    display.println("Wi-Fi failed.");
    display.println("Alerts local only.");
  }
  display.display();
  delay(1000);

  lastWifiRetryMs = millis();
}

// Re-attempts the Wi-Fi connection (non-blocking) if it's currently down and
// enough time has passed since the last attempt. Called every loop so a
// connection that failed at boot, or dropped later, keeps getting retried.
void maintainWiFi() {
  if (WiFi.status() == WL_CONNECTED) {
    return;
  }
  if (millis() - lastWifiRetryMs < WIFI_RETRY_INTERVAL_MS) {
    return;
  }
  lastWifiRetryMs = millis();
  WiFi.disconnect();
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

String urlEncode(const String &value) {
  String encoded = "";
  char buf[4];
  for (size_t i = 0; i < value.length(); i++) {
    char c = value.charAt(i);
    if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') {
      encoded += c;
    } else if (c == ' ') {
      encoded += "%20";
    } else {
      snprintf(buf, sizeof(buf), "%%%02X", (unsigned char)c);
      encoded += buf;
    }
  }
  return encoded;
}

// Sends a message through the Telegram bot. Returns true on a 2xx response.
bool sendTelegramAlert(const String &message) {
  if (WiFi.status() != WL_CONNECTED) {
    lastTelegramStatus = "no Wi-Fi";
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure(); // skip TLS cert validation - simplest option for a hobby project

  HTTPClient https;
  String url = "https://api.telegram.org/bot" + String(TELEGRAM_BOT_TOKEN) +
               "/sendMessage?chat_id=" + String(TELEGRAM_CHAT_ID) +
               "&text=" + urlEncode(message);

  bool ok = false;
  if (https.begin(client, url)) {
    int httpCode = https.GET();
    ok = (httpCode >= 200 && httpCode < 300);
    lastTelegramStatus = ok ? "sent" : ("HTTP " + String(httpCode));
    https.end();
  } else {
    lastTelegramStatus = "connect failed";
  }
  return ok;
}

void setup() {
  Serial.begin(115200);

  pinMode(FLAME_DO_PIN, INPUT_PULLUP); // avoids a floating pin reading a false LOW (fire) if the sensor is disconnected
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

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

  connectWiFi();
}

void loop() {
  maintainWiFi();

  int digitalReading = digitalRead(FLAME_DO_PIN); // LOW = flame detected
  int analogReading = analogRead(FLAME_AO_PIN);   // 0-4095, lower = stronger flame

  bool fireDetected = (digitalReading == LOW) || (analogReading < ANALOG_ALERT_THRESHOLD);

  digitalWrite(BUZZER_PIN, (BUZZER_ENABLED && fireDetected) ? HIGH : LOW);
  updateAlertLed(fireDetected);

  Serial.printf("DO=%d  AO=%d  Fire=%s\n",
                digitalReading, analogReading, fireDetected ? "YES" : "no");

  // Send a Telegram alert the moment fire is first detected, then re-send
  // periodically while it's still ongoing so the alert isn't a one-off.
  bool justStarted = fireDetected && !lastFireState;
  bool stillOngoing = fireDetected && lastFireState &&
                       (millis() - lastTelegramSendMs >= TELEGRAM_RESEND_INTERVAL_MS);

  if (justStarted || stillOngoing) {
    String message = "Fire detected! Analog reading: " + String(analogReading) +
                      ", digital: " + String(digitalReading == LOW ? "TRIGGERED" : "clear");
    sendTelegramAlert(message);
    lastTelegramSendMs = millis();
  }
  lastFireState = fireDetected;

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

  display.setCursor(0, 46);
  display.print("Telegram: ");
  display.println(lastTelegramStatus);

  display.setCursor(0, 56);
  display.println(fireDetected ? "!! CHECK AREA !!" : "Status: normal");

  display.display();

  delay(300);
}
