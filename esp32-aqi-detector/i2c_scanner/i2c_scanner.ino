/*
  I2C Scanner -- diagnostic tool for the ESP32 AQI detector.

  Upload this alone (separate sketch) if the OLED shows nothing. It
  scans the I2C bus and prints every address that responds. Your
  SSD1306 should show up as 0x3C or 0x3D. If nothing is printed at
  all, the problem is wiring or power, not the address.

  Wiring: SDA -> GPIO21, SCL -> GPIO22, VCC -> 3V3, GND -> GND
  (same pins the main sketch uses).
*/

#include <Wire.h>

void setup() {
  Wire.begin(21, 22); // SDA, SCL
  Serial.begin(115200);
  while (!Serial) delay(10);
  Serial.println("\nI2C scanner starting...");
}

void loop() {
  int found = 0;

  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    uint8_t error = Wire.endTransmission();

    if (error == 0) {
      Serial.print("Device found at 0x");
      if (addr < 16) Serial.print("0");
      Serial.println(addr, HEX);
      found++;
    }
  }

  if (found == 0) {
    Serial.println("No I2C devices found -- check wiring/power, not the address.");
  } else {
    Serial.print(found);
    Serial.println(" device(s) found.");
  }

  delay(3000);
}
