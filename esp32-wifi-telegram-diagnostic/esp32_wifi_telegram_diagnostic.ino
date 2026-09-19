/*
  ESP32 Wi-Fi + Telegram Diagnostic
  ------------------------------------
  A standalone troubleshooting sketch for the ESP32 fire sensor project's
  two most common failure points:

    1. "Is my Wi-Fi network actually 2.4GHz, and can the ESP32 see it?"
       The ESP32 DevKit V1's built-in radio is 2.4GHz-only (802.11 b/g/n)
       hardware -- it cannot join a 5GHz network, full stop. So if it
       connects at all, it is on 2.4GHz by definition. The real question
       is usually whether the router is broadcasting a 2.4GHz signal under
       that SSID at all (many dual-band routers use "band steering" to
       merge 2.4GHz and 5GHz under one name, and some hide or disable the
       2.4GHz radio). This sketch scans for nearby networks -- which, on
       ESP32 hardware, can only ever be 2.4GHz networks -- so you can
       confirm your target SSID actually shows up.

    2. "Is my Telegram alert actually being delivered?"
       Sends one test message through your bot and prints Telegram's raw
       HTTP response to Serial, so a failure (bad token, bad chat ID, bot
       never messaged, etc) is immediately visible instead of just an
       opaque HTTP status code.

  Fill in WIFI_SSID / WIFI_PASSWORD / TELEGRAM_BOT_TOKEN / TELEGRAM_CHAT_ID
  below (same values as esp32_fire_sensor.ino), upload, then open Serial
  Monitor at 115200 baud. The diagnostic re-runs automatically every 60
  seconds so you can watch it live while changing router settings.

  Required: no extra libraries beyond what ships with the ESP32 board
  package (WiFi, WiFiClientSecure, HTTPClient).
*/

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ctype.h>

// ---- Fill these in (same values as esp32_fire_sensor.ino) ----
const char *WIFI_SSID = "YOUR_WIFI_SSID";
const char *WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
const char *TELEGRAM_BOT_TOKEN = "YOUR_TELEGRAM_BOT_TOKEN";
const char *TELEGRAM_CHAT_ID = "YOUR_TELEGRAM_CHAT_ID";

const unsigned long WIFI_CONNECT_TIMEOUT_MS = 20000;
const unsigned long RECHECK_INTERVAL_MS = 60000;

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

// Scans nearby Wi-Fi networks and prints SSID/signal/channel for each.
// On ESP32 hardware every result here is necessarily a 2.4GHz network --
// the radio has no 5GHz receiver, so a 5GHz-only network simply won't
// appear in this list at all.
void scanNetworks() {
  Serial.println("Scanning for nearby Wi-Fi networks (2.4GHz only -- that's all this hardware can see)...");

  int n = WiFi.scanNetworks();
  if (n <= 0) {
    Serial.println("  No networks found at all. Check the ESP32 is powered and antenna/board is intact.");
    return;
  }

  bool targetFound = false;
  for (int i = 0; i < n; i++) {
    String ssid = WiFi.SSID(i);
    Serial.printf("  %2d: %-32s  RSSI:%5d dBm  channel:%2d  %s\n",
                  i + 1, ssid.c_str(), WiFi.RSSI(i), WiFi.channel(i),
                  (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? "open" : "secured");
    if (ssid == WIFI_SSID) {
      targetFound = true;
    }
  }

  if (targetFound) {
    Serial.println("  -> Target SSID \"" + String(WIFI_SSID) + "\" was found (2.4GHz). The ESP32 should be able to connect.");
  } else {
    Serial.println("  -> Target SSID \"" + String(WIFI_SSID) +
                    "\" was NOT found. Since this scan only sees 2.4GHz networks, this usually means: "
                    "(a) that SSID's 2.4GHz radio is off/hidden on your router (common with band-steering "
                    "dual-band routers that merge 2.4GHz+5GHz under one name), (b) the ESP32 is out of range, "
                    "or (c) the SSID is misspelled in WIFI_SSID.");
  }

  WiFi.scanDelete();
}

// Connects to WIFI_SSID/WIFI_PASSWORD and reports the result, including
// confirmation that a successful connection is necessarily 2.4GHz.
bool connectWiFiAndReport() {
  Serial.print("Connecting to \"" + String(WIFI_SSID) + "\"");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_CONNECT_TIMEOUT_MS) {
    delay(300);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("FAILED to connect (status code " + String(WiFi.status()) +
                    "). Double-check WIFI_SSID/WIFI_PASSWORD, and that the network is 2.4GHz-capable.");
    return false;
  }

  Serial.println("Connected!");
  Serial.println("  IP address:  " + WiFi.localIP().toString());
  Serial.println("  Signal:      " + String(WiFi.RSSI()) + " dBm");
  Serial.println("  Channel:     " + String(WiFi.channel()));
  Serial.println("  Band:        2.4GHz -- the ESP32's radio is 2.4GHz-only hardware, so any "
                  "successful connection is on the 2.4GHz band; there is no 5GHz mode to check.");
  return true;
}

// Sends one test message through the Telegram bot and prints the full
// result (status code + response body) to Serial.
void sendTelegramTestAndReport() {
  Serial.println("Sending Telegram test message...");

  WiFiClientSecure client;
  client.setInsecure(); // skip TLS cert validation - simplest option for a hobby project

  HTTPClient https;
  String message = "ESP32 diagnostic: this is a test alert. If you received this, Telegram delivery is working.";
  String url = "https://api.telegram.org/bot" + String(TELEGRAM_BOT_TOKEN) +
               "/sendMessage?chat_id=" + String(TELEGRAM_CHAT_ID) +
               "&text=" + urlEncode(message);

  if (!https.begin(client, url)) {
    Serial.println("  FAILED: https.begin() returned false (malformed URL or TLS setup issue).");
    return;
  }

  int httpCode = https.GET();
  String body = https.getString();
  https.end();

  if (httpCode >= 200 && httpCode < 300) {
    Serial.println("  SUCCESS (HTTP " + String(httpCode) + "). Check your Telegram chat now.");
  } else {
    Serial.println("  FAILED (HTTP " + String(httpCode) + ")");
    Serial.println("  Response: " + body);
    Serial.println("  Common causes: HTTP 404 = wrong/placeholder TELEGRAM_BOT_TOKEN; "
                    "HTTP 400 = wrong/placeholder TELEGRAM_CHAT_ID; "
                    "HTTP 403 = you haven't sent the bot a message yet (or you blocked it).");
  }
}

void runDiagnostic() {
  scanNetworks();
  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    connectWiFiAndReport();
  } else {
    Serial.println("Already connected. Channel " + String(WiFi.channel()) +
                    " (2.4GHz), RSSI " + String(WiFi.RSSI()) + " dBm.");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    sendTelegramTestAndReport();
  } else {
    Serial.println("Skipping Telegram test -- no Wi-Fi connection.");
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== ESP32 Wi-Fi + Telegram Diagnostic ===\n");
  runDiagnostic();
  Serial.println("\n=== Diagnostic complete. Re-running every " +
                  String(RECHECK_INTERVAL_MS / 1000) + "s. ===");
}

void loop() {
  delay(RECHECK_INTERVAL_MS);
  Serial.println("\n\n=== Re-running diagnostic ===\n");
  runDiagnostic();
}
