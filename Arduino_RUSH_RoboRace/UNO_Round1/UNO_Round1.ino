/*
  Arduino UNO - RUSH Robo Race Main Controller (Round 1: Qualifier)
  ---------------------------------------------------
  Standalone Uno-only sketch - the Nano (IR array + colour
  sensor), the original left ultrasonic sensor, and the right
  ultrasonic sensor are all unused here. Only the MIDDLE
  ultrasonic sensor drives behaviour:
    object detected (reading > 0 and <= OBJECT_DETECT_CM) -> go forward
    object not detected (reading 0/timeout, or farther than
    OBJECT_DETECT_CM)                                      -> turn left
  Checked continuously every loop, no pulsing - a straight
  continuous pivot while turning left. Note: a continuous pivot
  never translates the robot's position, so if no wall is in
  range from wherever it stops, it will spin in place until one
  comes back into view (this was previously mitigated with
  pulsing, which has been removed here per request).

  Wiring (Arduino Uno):
    L298N Motor Driver (direction pins):
      IN1 -> D4   (right motor forward)
      IN2 -> D5   (right motor reverse)
      IN3 -> D6   (left motor forward)
      IN4 -> D7   (left motor reverse)
      ENA / ENB -> tied directly to 5V (full-speed only,
                   no PWM speed control pins were wired)

    Middle Ultrasonic Sensor (middle of the chassis, fixed
    pointing sideways-left):
      TRIG -> D2
      ECHO -> D3
*/

// ---------- L298N motor driver ----------
const uint8_t IN1 = 4; // right motor forward
const uint8_t IN2 = 5; // right motor reverse
const uint8_t IN3 = 6; // left motor forward
const uint8_t IN4 = 7; // left motor reverse

// ---------- Middle ultrasonic sensor ----------
const uint8_t MIDDLE_TRIG = 2;
const uint8_t MIDDLE_ECHO = 3;

// ---------- Tunable (cm) ----------
const int OBJECT_DETECT_CM = 30; // reading at or below this = object detected

const unsigned long START_DELAY_MS = 3000; // time to place the robot before it moves

void setup() {
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);

  pinMode(MIDDLE_TRIG, OUTPUT);
  pinMode(MIDDLE_ECHO, INPUT);

  stopMotors();
  delay(START_DELAY_MS);
}

void loop() {
  long middleDist = readDistanceCm(MIDDLE_TRIG, MIDDLE_ECHO);
  bool objectDetected = (middleDist > 0 && middleDist <= OBJECT_DETECT_CM);

  if (objectDetected) {
    bothForward();
  } else {
    turnLeftSlightly();
  }
}

// ---------------------------------------------------
// Ultrasonic distance (cm), 0 on timeout / no echo
// ---------------------------------------------------
long readDistanceCm(uint8_t trigPin, uint8_t echoPin) {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  long duration = pulseIn(echoPin, HIGH, 25000UL); // 25ms timeout (~4m)
  if (duration == 0) return 0;
  return duration / 29 / 2; // speed of sound conversion to cm
}

// ---------------------------------------------------
// Motor primitives (L298N, direction pins only)
// ---------------------------------------------------
void bothForward() {
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, HIGH);
  digitalWrite(IN4, LOW);
}

void turnLeftSlightly() {
  // Left wheel (IN3/IN4) stopped, right wheel (IN1/IN2) keeps
  // driving forward - the robot pivots left, back toward the wall.
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, LOW);
}

void stopMotors() {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, LOW);
}
