/*
  SG90 servo test for the ESP32 DevKit V1 - no libraries needed.
  ---------------------------------------------------------------
  Moves the servo 0 -> 90 -> 180 -> 90 degrees, one second each, forever.
  Use it to check the servo wiring and power on their own.

  Wiring:
    SG90 orange (signal) -> D13 (GPIO13)
    SG90 red    (+5V)    -> VIN   (board must be powered by USB)
    SG90 brown  (GND)    -> GND

  Open the Serial Monitor at 115200 baud to see each angle as it is sent.
*/

const int SERVO_PIN = 13;
const int PWM_FREQ = 50;        // servos expect a pulse every 20 ms
const int PWM_RESOLUTION = 16;  // duty range 0..65535
const int MIN_PULSE_US = 500;   // 0 degrees
const int MAX_PULSE_US = 2400;  // 180 degrees

#if ESP_ARDUINO_VERSION_MAJOR < 3
const int PWM_CHANNEL = 0;      // ESP32 board package 2.x needs a channel number
#endif

void writeAngle(int angle) {
  int pulseUs = map(angle, 0, 180, MIN_PULSE_US, MAX_PULSE_US);
  uint32_t duty = (uint32_t)pulseUs * 65535 / 20000;
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(SERVO_PIN, duty);
#else
  ledcWrite(PWM_CHANNEL, duty);
#endif
  Serial.printf("Angle %3d deg -> pulse %d us\n", angle, pulseUs);
}

void setup() {
  Serial.begin(115200);
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(SERVO_PIN, PWM_FREQ, PWM_RESOLUTION);
#else
  ledcSetup(PWM_CHANNEL, PWM_FREQ, PWM_RESOLUTION);
  ledcAttachPin(SERVO_PIN, PWM_CHANNEL);
#endif
  Serial.println("Servo test starting");
}

void loop() {
  const int angles[] = { 0, 90, 180, 90 };
  for (int angle : angles) {
    writeAngle(angle);
    delay(1000);
  }
}
