/*
  Arduino NANO - Sensor Hub
  ---------------------------------------------------
  Reads the 5-way IR line sensor array and the TCS3200
  colour sensor, then streams the readings to the UNO
  over the hardware serial line (D0/D1) once per loop.

  Wiring (Arduino Nano):
    5-Way IR Sensor Array:
      IR1 -> D2
      IR2 -> D3
      IR3 -> D10
      IR4 -> D11
      IR5 -> D9

    TCS3200 Colour Sensor:
      S0  -> D5
      S1  -> D4
      S2  -> D7
      S3  -> D6
      OUT -> D8

    Serial link to UNO:
      Nano TX (D1) -> Uno RX (D0)
      Nano RX (D0) -> Uno TX (D1)
      GND <-> GND (common ground, required)

  NOTE: Because this link uses the hardware Serial pins
  (D0/D1), disconnect the cross-wiring to the UNO (or
  unplug the Nano's USB) while uploading this sketch,
  otherwise the upload can fail/garble.
*/

// ---------- IR Sensor Array pins ----------
const uint8_t IR1 = 2;
const uint8_t IR2 = 3;
const uint8_t IR3 = 10;
const uint8_t IR4 = 11;
const uint8_t IR5 = 9;

// ---------- TCS3200 Colour Sensor pins ----------
const uint8_t S0 = 5;
const uint8_t S1 = 4;
const uint8_t S2 = 7;
const uint8_t S3 = 6;
const uint8_t OUT_PIN = 8;

void setup() {
  Serial.begin(9600);

  pinMode(IR1, INPUT);
  pinMode(IR2, INPUT);
  pinMode(IR3, INPUT);
  pinMode(IR4, INPUT);
  pinMode(IR5, INPUT);

  pinMode(S0, OUTPUT);
  pinMode(S1, OUTPUT);
  pinMode(S2, OUTPUT);
  pinMode(S3, OUTPUT);
  pinMode(OUT_PIN, INPUT);

  // 20% frequency scaling (standard for TCS3200 line/colour work)
  digitalWrite(S0, HIGH);
  digitalWrite(S1, LOW);
}

// Reads the pulse width (us) coming out of the TCS3200 for one colour
// filter. Shorter pulse width = more intense colour.
unsigned long readColourFilter(bool s2State, bool s3State) {
  digitalWrite(S2, s2State ? HIGH : LOW);
  digitalWrite(S3, s3State ? HIGH : LOW);
  delayMicroseconds(50);
  return pulseIn(OUT_PIN, LOW, 25000UL); // 25ms timeout
}

String classifyColour(unsigned long red, unsigned long green, unsigned long blue) {
  // A 0 reading means pulseIn timed out (no light / sensor not settled)
  if (red == 0 && green == 0 && blue == 0) return "NONE";

  // Treat 0 (timeout) as "very weak" so it never wins the "smallest wins" comparison
  unsigned long r = (red == 0) ? 999999UL : red;
  unsigned long g = (green == 0) ? 999999UL : green;
  unsigned long b = (blue == 0) ? 999999UL : blue;

  if (r <= g && r <= b) return "RED";
  if (g <= r && g <= b) return "GREEN";
  return "BLUE";
}

void loop() {
  // ---- IR array (5 sensors, left to right) ----
  int ir1 = digitalRead(IR1);
  int ir2 = digitalRead(IR2);
  int ir3 = digitalRead(IR3);
  int ir4 = digitalRead(IR4);
  int ir5 = digitalRead(IR5);

  // ---- Colour sensor ----
  unsigned long red   = readColourFilter(LOW, LOW);
  unsigned long blue  = readColourFilter(LOW, HIGH);
  unsigned long green = readColourFilter(HIGH, HIGH);
  String colour = classifyColour(red, green, blue);

  // ---- Send frame to UNO: IR1,IR2,IR3,IR4,IR5,COLOUR ----
  Serial.print(ir1); Serial.print(',');
  Serial.print(ir2); Serial.print(',');
  Serial.print(ir3); Serial.print(',');
  Serial.print(ir4); Serial.print(',');
  Serial.print(ir5); Serial.print(',');
  Serial.println(colour);

  delay(20);
}
