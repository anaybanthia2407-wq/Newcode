/*
  ESP32 DevKit V1 + SSD1306 OLED Clock
  -------------------------------------
  Connects to Wi-Fi, syncs time via NTP, then displays a live
  HH:MM:SS clock plus the date on a 128x64 I2C OLED.

  Wiring (SSD1306 I2C module -> ESP32 DevKit V1):
    VCC -> 3V3
    GND -> GND
    SDA -> GPIO21
    SCL -> GPIO22

  Required libraries (install via Arduino Library Manager):
    - Adafruit GFX Library
    - Adafruit SSD1306
*/

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <time.h>

// ---- Wi-Fi credentials ----
const char *WIFI_SSID = "YOUR_WIFI_SSID";
const char *WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// ---- NTP / timezone settings ----
// GMT offset and daylight offset are in seconds.
// Example below is for India Standard Time (UTC+5:30, no DST).
// Adjust for your timezone: e.g. US Eastern -> gmtOffset_sec = -5*3600, daylightOffset_sec = 3600
const char *NTP_SERVER = "pool.ntp.org";
const long GMT_OFFSET_SEC = 5 * 3600 + 1800;
const int DAYLIGHT_OFFSET_SEC = 0;

// ---- OLED settings ----
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

const char *DAY_NAMES[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
const char *MONTH_NAMES[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                              "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

void connectWiFi() {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Connecting to");
  display.println(WIFI_SSID);
  display.display();

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    display.print(".");
    display.display();
  }

  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Wi-Fi connected!");
  display.println(WiFi.localIP());
  display.display();
  delay(1000);
}

void setup() {
  Serial.begin(115200);

  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println("SSD1306 allocation failed");
    while (true) delay(1000);
  }

  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  connectWiFi();

  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);

  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Syncing time...");
  display.display();

  struct tm timeinfo;
  while (!getLocalTime(&timeinfo)) {
    delay(500);
  }
}

void loop() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("Time sync lost");
    display.display();
    delay(1000);
    return;
  }

  char timeStr[9];
  snprintf(timeStr, sizeof(timeStr), "%02d:%02d:%02d",
           timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

  char dateStr[24];
  snprintf(dateStr, sizeof(dateStr), "%s, %d %s %d",
           DAY_NAMES[timeinfo.tm_wday], timeinfo.tm_mday,
           MONTH_NAMES[timeinfo.tm_mon], timeinfo.tm_year + 1900);

  display.clearDisplay();

  // Large centered time
  display.setTextSize(3);
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(timeStr, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, 12);
  display.println(timeStr);

  // Smaller date below
  display.setTextSize(1);
  display.getTextBounds(dateStr, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, 48);
  display.println(dateStr);

  display.display();

  delay(200);
}
