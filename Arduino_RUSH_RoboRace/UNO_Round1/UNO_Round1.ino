/*
  Arduino UNO - RUSH Robo Race Main Controller (Round 1: Qualifier)
  ---------------------------------------------------
  Standalone Uno-only sketch - the Nano (IR array + colour
  sensor) and the right ultrasonic sensor are both unused here.

  Two left-side ultrasonic sensors, both fixed pointing sideways-
  left (no servo): the original LEFT sensor (near the front) and
  a new MIDDLE sensor (mounted in the middle of the chassis),
  rechecked every loop:
    left distance < TOO_CLOSE_CM     -> turn right slightly (steer
                                         away) until back at 10cm
    BOTH left AND middle read 0
    (nothing in range on either one) -> turn left slightly (steer
                                         toward the wall) to reacquire it
    otherwise                        -> go straight
  Requiring both sensors to lose the wall before turning left
  avoids a false "wall lost" turn from a single sensor's blind
  spot or a momentary bad reading.

  Wiring (Arduino Uno):
    L298N Motor Driver (direction pins):
      IN1 -> D4   (right motor forward)
      IN2 -> D5   (right motor reverse)
      IN3 -> D6   (left motor forward)
      IN4 -> D7   (left motor reverse)
      ENA / ENB -> tied directly to 5V (full-speed only,
                   no PWM speed control pins were wired)

    Left Ultrasonic Sensor (front, fixed pointing sideways-left):
      TRIG -> D8
      ECHO -> D9

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

// ---------- Left ultrasonic sensors ----------
const uint8_t LEFT_TRIG = 8;
const uint8_t LEFT_ECHO = 9;
const uint8_t MIDDLE_TRIG = 2;
const uint8_t MIDDLE_ECHO = 3;

// ---------- Tunable (cm) ----------
const int TOO_CLOSE_CM = 10; // below this -> steer away; this is also the target hugging distance

const unsigned long START_DELAY_MS = 3000; // time to place the robot before it moves

void setup() {
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);

  pinMode(LEFT_TRIG, OUTPUT);
  pinMode(LEFT_ECHO, INPUT);
  pinMode(MIDDLE_TRIG, OUTPUT);
  pinMode(MIDDLE_ECHO, INPUT);

  stopMotors();
  delay(START_DELAY_MS);
}

void loop() {
  long leftDist = readDistanceCm(LEFT_TRIG, LEFT_ECHO);
  long middleDist = readDistanceCm(MIDDLE_TRIG, MIDDLE_ECHO);

  if (leftDist > 0 && leftDist < TOO_CLOSE_CM) {
    turnRightSlightly();  // too close to the wall - steer away
  } else if (leftDist == 0 && middleDist == 0) {
    turnLeftSlightly();   // both sensors lost the wall - steer back toward it
  } else {
    bothForward();        // holding a good distance - straight ahead
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

void turnRightSlightly() {
  // Right wheel (IN1/IN2) stopped, left wheel (IN3/IN4) keeps
  // driving forward - the robot pivots right, away from the wall.
  digitalWrite(IN1, LOW);
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
