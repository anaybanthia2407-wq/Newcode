/*
  Arduino NANO - RUSH Robo Race Sensor Hub (Round 2: Semi-Final)
  ---------------------------------------------------
  Only the TCS3200 colour sensor is used this round - the IR
  array is not read. The colour sensor is mounted on the left
  servo, which is assumed fixed at its centre angle (no servo
  control happens here or on the Uno). Streams just the colour
  classification to the UNO over hardware serial (D0/D1), one
  line per loop.

  Wiring (Arduino Nano):
    TCS3200 Colour Sensor:
      OE  -> GND (tied permanently, not wired to a pin)
      S0  -> D8
      S1  -> D9
      S2  -> D10
      S3  -> D11
      OUT -> D12

    Serial link to UNO:
      Nano TX (D1) -> Uno RX (D0)
      Nano RX (D0) -> Uno TX (D1)
      GND <-> GND (common ground, required)

  NOTE: Because this link uses the hardware Serial pins
  (D0/D1), disconnect the cross-wiring to the UNO (or
  unplug the Nano's USB) while uploading this sketch.
*/

// ---------- TCS3200 colour sensor pins ----------
const uint8_t COLOUR_S0 = 8;
const uint8_t COLOUR_S1 = 9;
const uint8_t COLOUR_S2 = 10;
const uint8_t COLOUR_S3 = 11;
const uint8_t COLOUR_OUT = 12;

void setup() {
  Serial.begin(9600);

  pinMode(COLOUR_S0, OUTPUT);
  pinMode(COLOUR_S1, OUTPUT);
  pinMode(COLOUR_S2, OUTPUT);
  pinMode(COLOUR_S3, OUTPUT);
  pinMode(COLOUR_OUT, INPUT);

  // 20% frequency scaling (standard for TCS3200 work). OE is
  // tied straight to GND on this build, so no pin controls it.
  digitalWrite(COLOUR_S0, HIGH);
  digitalWrite(COLOUR_S1, LOW);
}

// Reads the pulse width (us) coming out of the TCS3200 for one
// colour filter. Shorter pulse width = more intense colour.
unsigned long readColourFilter(bool s2State, bool s3State) {
  digitalWrite(COLOUR_S2, s2State ? HIGH : LOW);
  digitalWrite(COLOUR_S3, s3State ? HIGH : LOW);
  delayMicroseconds(50);
  return pulseIn(COLOUR_OUT, LOW, 25000UL); // 25ms timeout
}

String classifyColour(unsigned long red, unsigned long green, unsigned long blue) {
  if (red == 0 && green == 0 && blue == 0) return "NONE";

  unsigned long r = (red == 0) ? 999999UL : red;
  unsigned long g = (green == 0) ? 999999UL : green;
  unsigned long b = (blue == 0) ? 999999UL : blue;

  if (r <= g && r <= b) return "RED";
  if (g <= r && g <= b) return "GREEN";
  return "BLUE";
}

void loop() {
  unsigned long red   = readColourFilter(LOW, LOW);
  unsigned long blue  = readColourFilter(LOW, HIGH);
  unsigned long green = readColourFilter(HIGH, HIGH);
  String colour = classifyColour(red, green, blue);

  Serial.println(colour);

  delay(15);
}
