/*
  Arduino UNO - RUSH Robo Race Main Controller (Round 1: Qualifier)
  ---------------------------------------------------
  Standalone Uno-only sketch - the Nano (IR array + colour
  sensor) is not used at all here.

  Simple bang-bang wall follower using only the two ultrasonic
  sensors:
    - LEFT ultrasonic (fixed pointing sideways-left) hugs
      whatever wall is beside the robot on the left:
        wall detected within range -> drive both wheels forward
        wall not detected (too far / lost it)  -> stop the LEFT
        wheel and keep the RIGHT wheel driving forward, curving
        the robot back left until the wall is picked up again
    - RIGHT ultrasonic (fixed pointing forward) is the collision
      recovery trigger: if it reads closer than RIGHT_STOP_CM, the
      robot stops, backs up, pivots left, then resumes normal
      left-wall-following - overriding everything else while it
      runs.

  Wiring (Arduino Uno):
    L298N Motor Driver (direction pins):
      IN1 -> D4   (left motor forward)
      IN2 -> D5   (left motor reverse)
      IN3 -> D6   (right motor forward)
      IN4 -> D7   (right motor reverse)
      ENA / ENB -> tied directly to 5V (full-speed only,
                   no PWM speed control pins were wired)

    Right Ultrasonic Sensor (mounted on the right servo horn,
    held pointing forward):
      TRIG -> D10
      ECHO -> D11

    Left Ultrasonic Sensor (mounted on the left servo horn,
    held pointing sideways-left):
      TRIG -> D8
      ECHO -> D9

    Servos (just hold each sensor at a fixed angle - no sweeping
    this round):
      Right servo -> A0
      Left servo  -> A1
*/

#include <Servo.h>

// ---------- L298N motor driver ----------
const uint8_t IN1 = 4; // left motor forward
const uint8_t IN2 = 5; // left motor reverse
const uint8_t IN3 = 6; // right motor forward
const uint8_t IN4 = 7; // right motor reverse

// ---------- Ultrasonic sensors ----------
const uint8_t RIGHT_TRIG = 10;
const uint8_t RIGHT_ECHO = 11;
const uint8_t LEFT_TRIG = 8;
const uint8_t LEFT_ECHO = 9;

// ---------- Servos ----------
const uint8_t RIGHT_SERVO_PIN = A0;
const uint8_t LEFT_SERVO_PIN = A1;
Servo rightServo;
Servo leftServo;

// Fixed mounting angles - depends on how each ultrasonic sensor
// sits on its servo horn, check physically and adjust.
const int RIGHT_FORWARD_ANGLE = 90;  // aims the right sensor straight ahead
const int LEFT_SIDE_ANGLE = 180;     // aims the left sensor straight out to the left

// ---------- Tunables (cm) ----------
const int LEFT_WALL_CM = 20;  // left sensor reading at or below this = "wall detected"
const int RIGHT_STOP_CM = 5;  // right sensor reading below this = trigger recovery

// ---------- Recovery manoeuvre timing (no encoders / no PWM speed control) ----------
const unsigned long RECOVERY_PAUSE_MS = 100;   // brief settle between each recovery step
const unsigned long BACKUP_MS = 400;           // how long to reverse for
const unsigned long RECOVERY_TURN_MS = 300;    // how long to pivot left for
const unsigned long START_DELAY_MS = 3000;     // time to place the robot before it moves

void setup() {
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);

  pinMode(RIGHT_TRIG, OUTPUT);
  pinMode(RIGHT_ECHO, INPUT);
  pinMode(LEFT_TRIG, OUTPUT);
  pinMode(LEFT_ECHO, INPUT);

  rightServo.attach(RIGHT_SERVO_PIN);
  leftServo.attach(LEFT_SERVO_PIN);
  rightServo.write(RIGHT_FORWARD_ANGLE);
  leftServo.write(LEFT_SIDE_ANGLE);

  stopMotors();
  delay(START_DELAY_MS);
}

void loop() {
  long rightDist = readDistanceCm(RIGHT_TRIG, RIGHT_ECHO);

  if (rightDist > 0 && rightDist < RIGHT_STOP_CM) {
    recoverFromFrontWall();
    return;
  }

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
// Recovery: back up, pivot left, then let the normal
// left-wall-following logic pick back up next loop.
// ---------------------------------------------------
void recoverFromFrontWall() {
  stopMotors();
  delay(RECOVERY_PAUSE_MS);

  moveBackward();
  delay(BACKUP_MS);
  stopMotors();
  delay(RECOVERY_PAUSE_MS);

  pivotLeft();
  delay(RECOVERY_TURN_MS);
  stopMotors();
  delay(RECOVERY_PAUSE_MS);
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
  // Left wheel stopped, right wheel keeps driving forward - the
  // robot arcs left until the left ultrasonic finds the wall again.
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, HIGH);
  digitalWrite(IN4, LOW);
}

void moveBackward() {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, HIGH);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, HIGH);
}

void pivotLeft() {
  // Both wheels turn opposite ways - spins in place, left.
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, HIGH);
  digitalWrite(IN3, HIGH);
  digitalWrite(IN4, LOW);
}

void stopMotors() {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, LOW);
}
