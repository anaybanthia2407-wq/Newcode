/*
  Arduino NANO - RUSH Robo Race Sensor Hub (Round 1: Qualifier)
  ---------------------------------------------------
  Reads the 5-channel IR array and streams it to the UNO over
  hardware serial (D0/D1) every loop.

  Physical mounting: this array is angled DOWN toward the ground
  (not forward/parallel like earlier revisions) and kept close to
  the surface (roughly 0.5-2cm), like a standard line/edge sensor.
  It's used to detect the black boundary tape marking the track
  edges - a flat tape line can't be reliably seen at a shallow
  forward-facing angle, it needs a near-perpendicular downward view.
  Each channel's onboard trim pot must be recalibrated after any
  remount: sweep the sensor across the tape boundary and adjust
  until that channel's LED reliably flips at the edge.

  Wiring (Arduino Nano):
    5-Channel IR boundary sensor (facing down, right to left):
      S1 (extreme right) -> D2
      S2                  -> D3
      S3 (centre)         -> D4
      S4                  -> D5
      S5 (extreme left)   -> D6

    Serial link to UNO:
      Nano TX (D1) -> Uno RX (D0)
      Nano RX (D0) -> Uno TX (D1)
      GND <-> GND (common ground, required)

  NOTE: Because this link uses the hardware Serial pins
  (D0/D1), disconnect the cross-wiring to the UNO (or
  unplug the Nano's USB) while uploading this sketch.
*/

// ---------- IR boundary sensor pins (right to left) ----------
const uint8_t IR_S1 = 2; // extreme right
const uint8_t IR_S2 = 3;
const uint8_t IR_S3 = 4; // centre
const uint8_t IR_S4 = 5;
const uint8_t IR_S5 = 6; // extreme left

void setup() {
  Serial.begin(9600);

  pinMode(IR_S1, INPUT);
  pinMode(IR_S2, INPUT);
  pinMode(IR_S3, INPUT);
  pinMode(IR_S4, INPUT);
  pinMode(IR_S5, INPUT);
}

void loop() {
  int s1 = digitalRead(IR_S1);
  int s2 = digitalRead(IR_S2);
  int s3 = digitalRead(IR_S3);
  int s4 = digitalRead(IR_S4);
  int s5 = digitalRead(IR_S5);

  // Frame: s1,s2,s3,s4,s5  (s1 = extreme right ... s5 = extreme left)
  Serial.print(s1); Serial.print(',');
  Serial.print(s2); Serial.print(',');
  Serial.print(s3); Serial.print(',');
  Serial.print(s4); Serial.print(',');
  Serial.println(s5);

  delay(15);
}
