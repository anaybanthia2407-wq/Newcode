/*
  Arduino UNO - RUSH Robo Race Main Controller (Round 1: Qualifier)
  ---------------------------------------------------
  Standalone Uno-only sketch - the Nano is not used here.

  FRONT ultrasonic (the "right ultrasonic sensor" hardware, Trig
  D10/Echo D11, now mounted facing straight forward instead of
  sideways) is the collision-recovery trigger - checked first,
  overriding everything else:
    distance < FRONT_WALL_CM -> back up, stop, then tank-turn
    (left wheel forward, right wheel backward) until the front
    sensor no longer sees the wall, then resume normal driving.

  LEFT ultrasonic (fixed pointing sideways-left, Trig D8/Echo D9)
  hugs whatever wall is beside the robot, with three graduated
  responses instead of a single on/off curve:
    distance > 60cm          -> wall essentially lost (e.g. past a
                                 corner) - full-strength turn left
                                 (curveTowardLeftWall, same as before)
    distance < 19cm          -> too close - pivot right, away from
                                 the wall, until back near 20cm
    distance > 21cm (<=60cm) -> moderately far - gentle nudge left,
                                 toward the wall, until back near 20cm
    19-21cm                  -> on target - drive straight

  Wiring (Arduino Uno):
    L298N Motor Driver (direction pins):
      IN1 -> D4   (right motor forward)
      IN2 -> D5   (right motor reverse)
      IN3 -> D6   (left motor forward)
      IN4 -> D7   (left motor reverse)
      ENA -> D3   (left motor speed, PWM)
      ENB -> tied directly to 5V (right motor stays full-speed,
             no PWM control was wired for it)

    Left Ultrasonic Sensor (mounted fixed, pointing sideways-left,
    no servo):
      TRIG -> D8
      ECHO -> D9

    Front Ultrasonic Sensor (mounted fixed, pointing straight
    forward):
      TRIG -> D10
      ECHO -> D11
*/

// ---------- L298N motor driver ----------
const uint8_t IN1 = 4; // right motor forward
const uint8_t IN2 = 5; // right motor reverse
const uint8_t IN3 = 6; // left motor forward
const uint8_t IN4 = 7; // left motor reverse
const uint8_t LEFT_ENA = 3; // left motor speed (PWM)

// ---------- Left ultrasonic sensor ----------
const uint8_t LEFT_TRIG = 8;
const uint8_t LEFT_ECHO = 9;

// ---------- Front ultrasonic sensor ----------
const uint8_t FRONT_TRIG = 10;
const uint8_t FRONT_ECHO = 11;

// ---------- Tunables (cm) ----------
const int LEFT_TARGET_MIN_CM = 19; // below this -> too close, pivot away
const int LEFT_TARGET_MAX_CM = 21; // above this -> too far, nudge toward wall
const int LEFT_LOST_CM = 60;       // above this -> wall essentially lost, full turn
const int FRONT_WALL_CM = 20;      // front sensor reading below this -> back up and turn

// ---------- Left motor speed (PWM, 0-255) ----------
const uint8_t LEFT_FULL_SPEED = 255;
const uint8_t LEFT_HALF_SPEED = 75;   // strong correction, used when the wall is essentially lost (>60cm)
const uint8_t LEFT_GENTLE_SPEED = 180; // milder correction, used for the 21-60cm fine nudge

// ---------- Front-wall recovery timing ----------
const unsigned long REVERSE_MS = 300; // how long to back up before turning

const unsigned long START_DELAY_MS = 3000; // time to place the robot before it moves

void setup() {
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);
  pinMode(LEFT_ENA, OUTPUT);

  pinMode(LEFT_TRIG, OUTPUT);
  pinMode(LEFT_ECHO, INPUT);
  pinMode(FRONT_TRIG, OUTPUT);
  pinMode(FRONT_ECHO, INPUT);

  stopMotors();
  delay(START_DELAY_MS);
}

void loop() {
  long frontDist = readDistanceCm(FRONT_TRIG, FRONT_ECHO);
  if (frontDist > 0 && frontDist < FRONT_WALL_CM) {
    avoidFrontWall();
    return;
  }

  long leftDist = readDistanceCm(LEFT_TRIG, LEFT_ECHO);

  if (leftDist == 0 || leftDist > LEFT_LOST_CM) {
    curveTowardLeftWall();
  } else if (leftDist < LEFT_TARGET_MIN_CM) {
    pivotAwayFromLeftWall();
  } else if (leftDist > LEFT_TARGET_MAX_CM) {
    gentleNudgeTowardLeftWall();
  } else {
    bothForward();
  }
}

// ---------------------------------------------------
// Back up, stop, then tank-turn left until the front sensor
// no longer sees the wall, then hand back to the main loop.
// ---------------------------------------------------
void avoidFrontWall() {
  moveBackward();
  delay(REVERSE_MS);
  stopMotors();

  tankTurnLeft();
  long frontDist;
  do {
    frontDist = readDistanceCm(FRONT_TRIG, FRONT_ECHO);
  } while (frontDist > 0 && frontDist < FRONT_WALL_CM);

  stopMotors();
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
// Motor primitives (L298N)
// ---------------------------------------------------
void bothForward() {
  analogWrite(LEFT_ENA, LEFT_FULL_SPEED);
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, HIGH);
  digitalWrite(IN4, LOW);
}

void moveBackward() {
  analogWrite(LEFT_ENA, LEFT_FULL_SPEED);
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, HIGH);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, HIGH);
}

void tankTurnLeft() {
  // Left wheel forward, right wheel backward - spins left in place.
  analogWrite(LEFT_ENA, LEFT_FULL_SPEED);
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, HIGH);
  digitalWrite(IN3, HIGH);
  digitalWrite(IN4, LOW);
}

void curveTowardLeftWall() {
  // Left wheel (IN3/IN4) kept forward but slowed via ENA (strong
  // correction), right wheel (IN1/IN2) stays at full speed - the
  // robot arcs left until the left ultrasonic finds the wall again.
  analogWrite(LEFT_ENA, LEFT_HALF_SPEED);
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, HIGH);
  digitalWrite(IN4, LOW);
}

void gentleNudgeTowardLeftWall() {
  // Same idea as curveTowardLeftWall() but a milder speed
  // reduction on the left wheel, for the smaller 21-60cm fine
  // correction rather than a full wall-recovery turn.
  analogWrite(LEFT_ENA, LEFT_GENTLE_SPEED);
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, HIGH);
  digitalWrite(IN4, LOW);
}

void pivotAwayFromLeftWall() {
  // Right wheel stopped, left wheel forward at full speed - pivots
  // right, away from the wall. The right motor has no PWM (ENB
  // tied to 5V), so a full stop is the only way to reduce its
  // contribution.
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  analogWrite(LEFT_ENA, LEFT_FULL_SPEED);
  digitalWrite(IN3, HIGH);
  digitalWrite(IN4, LOW);
}

void stopMotors() {
  analogWrite(LEFT_ENA, LEFT_FULL_SPEED);
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, LOW);
}
