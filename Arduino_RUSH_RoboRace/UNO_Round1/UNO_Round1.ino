/*
  Arduino UNO - RUSH Robo Race Main Controller (Round 1: Qualifier)
  ---------------------------------------------------
  Round 1 has no obstacles (per the rules, obstacles start in
  the Semi-Final) - the course is judged on speed alone. So
  this sketch does not use the colour sensor at all yet; it
  just races around the loop as fast as it safely can.

  Strategy: hug the inner island wall closely with one fixed
  sideways-facing ultrasonic sensor, watch the outer wall from
  a distance with the other, and use the forward-facing IR
  bumper array as a last-resort "wall dead ahead" check.

  Wiring (Arduino Uno):
    L298N Motor Driver (direction pins):
      IN1 -> D4
      IN2 -> D5
      IN3 -> D6
      IN4 -> D7
      ENA / ENB -> tied directly to 5V (full-speed only,
                   no PWM speed control pins were wired)

    Right Ultrasonic Sensor (mounted on the right servo horn):
      TRIG -> D10
      ECHO -> D11

    Left Ultrasonic Sensor (mounted on the left servo horn):
      TRIG -> D8
      ECHO -> D9

    Servos (each carries one ultrasonic sensor on its horn -
    kept at a fixed sideways angle this round since there are
    no obstacles to scan for yet):
      Right servo -> A0
      Left servo  -> A1

    Serial link to NANO:
      Uno TX (D1) -> Nano RX (D0)
      Uno RX (D0) -> Nano TX (D1)
      GND <-> GND (common ground, required)

  NOTE: Because this link uses the hardware Serial pins
  (D0/D1), disconnect the cross-wiring to the NANO (or
  unplug the Uno's USB) while uploading this sketch.
*/

#include <Servo.h>

// ---------- L298N motor driver ----------
const uint8_t IN1 = 4;
const uint8_t IN2 = 5;
const uint8_t IN3 = 6;
const uint8_t IN4 = 7;

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

// Angles that aim each ultrasonic sensor straight out to its
// own side. Depends entirely on how the sensor is mounted on
// the servo horn - check physically and adjust these two.
const int RIGHT_SIDE_ANGLE = 0;
const int LEFT_SIDE_ANGLE = 180;

// ---------- Which side the island is on ----------
// true  = robot loops with the island on its LEFT  (Left ultrasonic = inner-wall sensor)
// false = robot loops with the island on its RIGHT (Right ultrasonic = inner-wall sensor)
// Set this to match whichever direction you actually drive the loop.
const bool ISLAND_ON_LEFT = true;

// ---------- IR bumper polarity ----------
const bool IR_ACTIVE_HIGH = true; // set false if your module outputs LOW when it senses something close

// ---------- Tunables (cm) ----------
const int INNER_MIN_CM = 8;   // closer than this to the island -> steer away
const int INNER_MAX_CM = 25;  // farther than this -> island corner has receded, steer back toward it
const int OUTER_MIN_CM = 15;  // outer wall closer than this -> steer back toward the island

// ---------- Motion timing (no encoders, no PWM speed control) ----------
const unsigned long CORRECTION_TURN_MS = 120; // brief steering pulse for normal wall-hugging
const unsigned long EMERGENCY_TURN_MS = 300;  // bigger turn when a wall is dead ahead
const unsigned long START_DELAY_MS = 3000;    // time to place the robot on the track before it moves

// ---------- Data from the Nano ----------
int irRight2Left[5] = {0, 0, 0, 0, 0}; // s1 (extreme right) .. s5 (extreme left)
String colour = "NONE"; // read but unused this round

void setup() {
  Serial.begin(9600);

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
  rightServo.write(RIGHT_SIDE_ANGLE);
  leftServo.write(LEFT_SIDE_ANGLE);

  stopMotors();
  delay(START_DELAY_MS);
}

void loop() {
  readNanoData();

  long rightDist = readDistanceCm(RIGHT_TRIG, RIGHT_ECHO);
  long leftDist = readDistanceCm(LEFT_TRIG, LEFT_ECHO);

  long islandDist = ISLAND_ON_LEFT ? leftDist : rightDist;
  long outerDist  = ISLAND_ON_LEFT ? rightDist : leftDist;

  if (isAnyFrontWall()) {
    // Wall dead ahead: steer away from the island until it clears.
    turnAwayFromIsland();
    delay(EMERGENCY_TURN_MS);
    stopMotors();
    return;
  }

  if (islandDist > 0 && islandDist < INNER_MIN_CM) {
    // Too close to the island - nudge away from it.
    turnAwayFromIsland();
    delay(CORRECTION_TURN_MS);
  } else if (islandDist == 0 || islandDist > INNER_MAX_CM) {
    // Island wall has receded (rounding its corner) - hug it around.
    turnTowardIsland();
    delay(CORRECTION_TURN_MS);
  } else if (outerDist > 0 && outerDist < OUTER_MIN_CM) {
    // Drifted too close to the outer wall - correct back inward.
    turnTowardIsland();
    delay(CORRECTION_TURN_MS);
  } else {
    moveForward();
  }
}

// ---------------------------------------------------
// Serial: parse "s1,s2,s3,s4,s5,COLOUR" from the Nano
// (s1 = extreme right ... s5 = extreme left)
// ---------------------------------------------------
void readNanoData() {
  static String buffer = "";

  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') {
      parseNanoFrame(buffer);
      buffer = "";
    } else if (c != '\r') {
      buffer += c;
    }
  }
}

void parseNanoFrame(String frame) {
  int start = 0;
  int field = 0;

  for (int i = 0; i < frame.length() && field < 5; i++) {
    if (frame.charAt(i) == ',') {
      irRight2Left[field] = frame.substring(start, i).toInt();
      start = i + 1;
      field++;
    }
  }
  if (field == 5) {
    colour = frame.substring(start);
    colour.trim();
  }
}

bool isAnyFrontWall() {
  for (int i = 0; i < 5; i++) {
    bool triggered = IR_ACTIVE_HIGH ? (irRight2Left[i] == HIGH) : (irRight2Left[i] == LOW);
    if (triggered) return true;
  }
  return false;
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
// Steering helpers: map "toward/away from the island" onto
// the physical left/right turn, based on ISLAND_ON_LEFT.
// ---------------------------------------------------
void turnTowardIsland() {
  if (ISLAND_ON_LEFT) turnLeft(); else turnRight();
}

void turnAwayFromIsland() {
  if (ISLAND_ON_LEFT) turnRight(); else turnLeft();
}

// ---------------------------------------------------
// Motor primitives (L298N, direction pins only)
// ---------------------------------------------------
void moveForward() {
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, HIGH);
  digitalWrite(IN4, LOW);
}

void turnLeft() {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, HIGH);
  digitalWrite(IN3, HIGH);
  digitalWrite(IN4, LOW);
}

void turnRight() {
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, HIGH);
}

void stopMotors() {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, LOW);
}
