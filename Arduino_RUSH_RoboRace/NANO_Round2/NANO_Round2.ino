/*
  Arduino NANO - RUSH Robo Race Sensor Hub (Round 2: Semi-Final)
  ---------------------------------------------------
  Same sensor hub as Round 1 - reads the forward-facing 5-channel
  IR wall/proximity array and the TCS3200 colour sensor, streams
  both to the UNO over hardware serial (D0/D1) every loop. Now
  actually acted on by the Uno: the IR array flags "an obstacle is
  ahead" and the colour tells it red (go around left) vs green
  (go around right).

  The IR array points straight forward, parallel to the ground
  (NOT down at the floor) - it senses how close something is
  directly ahead of the robot.

  Wiring (Arduino Nano):
    5-Channel IR wall/obstacle sensor (front bumper, right to left):
      S1 (extreme right) -> D2
      S2                  -> D3
      S3 (centre)         -> D4
      S4                  -> D5
      S5 (extreme left)   -> D6

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

// ---------- IR wall/obstacle sensor pins (right to left) ----------
const uint8_t IR_S1 = 2; // extreme right
const uint8_t IR_S2 = 3;
const uint8_t IR_S3 = 4; // centre
const uint8_t IR_S4 = 5;
const uint8_t IR_S5 = 6; // extreme left

// ---------- TCS3200 colour sensor pins ----------
const uint8_t COLOUR_S0 = 8;
const uint8_t COLOUR_S1 = 9;
const uint8_t COLOUR_S2 = 10;
const uint8_t COLOUR_S3 = 11;
const uint8_t COLOUR_OUT = 12;

void setup() {
  Serial.begin(9600);

  pinMode(IR_S1, INPUT);
  pinMode(IR_S2, INPUT);
  pinMode(IR_S3, INPUT);
  pinMode(IR_S4, INPUT);
  pinMode(IR_S5, INPUT);

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
  int s1 = digitalRead(IR_S1);
  int s2 = digitalRead(IR_S2);
  int s3 = digitalRead(IR_S3);
  int s4 = digitalRead(IR_S4);
  int s5 = digitalRead(IR_S5);

  unsigned long red   = readColourFilter(LOW, LOW);
  unsigned long blue  = readColourFilter(LOW, HIGH);
  unsigned long green = readColourFilter(HIGH, HIGH);
  String colour = classifyColour(red, green, blue);

  // Frame: s1,s2,s3,s4,s5,COLOUR  (s1 = extreme right ... s5 = extreme left)
  Serial.print(s1); Serial.print(',');
  Serial.print(s2); Serial.print(',');
  Serial.print(s3); Serial.print(',');
  Serial.print(s4); Serial.print(',');
  Serial.print(s5); Serial.print(',');
  Serial.println(colour);

  delay(15);
}
