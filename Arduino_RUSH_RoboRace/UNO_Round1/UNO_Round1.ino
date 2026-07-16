/*
  Arduino UNO - RUSH Robo Race Main Controller (Round 1: Qualifier)
  ---------------------------------------------------
  Standalone Uno-only sketch - the Nano (IR array + colour
  sensor) and the right ultrasonic sensor are both unused here.

  Left ultrasonic (fixed pointing sideways-left) hugs whatever
  wall is beside the robot on the left:
    wall detected within range -> drive both wheels forward
    wall not detected (too far / lost it) -> stop the LEFT wheel
    and keep the RIGHT wheel driving forward, curving the robot
    back left until the wall is picked up again

  Wiring (Arduino Uno):
    L298N Motor Driver (direction pins):
      IN1 -> D4   (right motor forward)
      IN2 -> D5   (right motor reverse)
      IN3 -> D6   (left motor forward)
      IN4 -> D7   (left motor reverse)
      ENA / ENB -> tied directly to 5V (full-speed only,
                   no PWM speed control pins were wired)

    Left Ultrasonic Sensor (mounted fixed, pointing sideways-left,
    no servo):
      TRIG -> D8
      ECHO -> D9
*/

// ---------- L298N motor driver ----------
const uint8_t IN1 = 4; // right motor forward
const uint8_t IN2 = 5; // right motor reverse
const uint8_t IN3 = 6; // left motor forward
const uint8_t IN4 = 7; // left motor reverse

// ---------- Left ultrasonic sensor ----------
const uint8_t LEFT_TRIG = 8;
const uint8_t LEFT_ECHO = 9;

// ---------- Tunable (cm) ----------
const int LEFT_WALL_CM = 20; // left sensor reading at or below this = "wall detected"

const unsigned long START_DELAY_MS = 3000; // time to place the robot before it moves

void setup() {
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);

  pinMode(LEFT_TRIG, OUTPUT);
  pinMode(LEFT_ECHO, INPUT);

  stopMotors();
  delay(START_DELAY_MS);
}

void loop() {
  long leftDist = readDistanceCm(LEFT_TRIG, LEFT_ECHO);
  bool leftWallDetected = (leftDist > 0 && leftDist <= LEFT_WALL_CM);

  if (leftWallDetected) {
    bothForward();
  } else {
    curveTowardLeftWall();
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

void curveTowardLeftWall() {
  // Left wheel (IN3/IN4) stopped, right wheel (IN1/IN2) keeps
  // driving forward - the robot arcs left until the left
  // ultrasonic finds the wall again.
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
