/*
  Arduino UNO - RUSH Robo Race Main Controller (Round 2: Semi-Final)
  ---------------------------------------------------
  Builds on the confirmed-working Round 1 left-wall follower.
  IR array and right ultrasonic sensor are NOT used in this
  version - the left servo is assumed fixed at its centre angle
  with the colour sensor mounted on it, so the colour sensor is
  the obstacle trigger itself: once it reads RED or GREEN (rather
  than NONE), an obstacle is right there.

  Manoeuvre once RED or GREEN is seen:
    1. Stop.
    2. Back up slightly.
    3. Sharp tank turn (one wheel forward, other backward) - left
       for red, right for green.
    4. Drive forward 0.75s - THIS is what has to get the obstacle
       fully behind the car. SHARP_TURN_MS and this first
       CLEAR_FORWARD_MS burst are the two values to lengthen if
       the obstacle isn't completely cleared by the end of this
       step.
    5. Sharp tank turn the OTHER way (equal duration to step 3,
       so the heading change cancels out). Only runs once the
       obstacle is already behind the car from step 4.
    6. Drive forward 0.75s - this second burst is unrelated to
       clearing the obstacle; it exists purely to close the gap
       back to the wall that steps 3-4 opened up.
  End state: same heading/distance from the wall as before the
  obstacle, just moved forward and now on the other side of it,
  with the obstacle behind the car. Control then returns to the
  Round 1 wall follower.

  Wiring (Arduino Uno):
    L298N Motor Driver:
      IN1 -> D4   (right motor forward)
      IN2 -> D5   (right motor reverse)
      IN3 -> D6   (left motor forward)
      IN4 -> D7   (left motor reverse)
      ENA -> D3   (left motor speed, PWM)
      ENB -> tied directly to 5V (right motor full-speed only)

    Left Ultrasonic Sensor (fixed, pointing sideways-left, no servo):
      TRIG -> D8
      ECHO -> D9

    Serial link to NANO (colour sensor, mounted on the left servo
    which is assumed fixed at centre - no servo control from here):
      Uno TX (D1) -> Nano RX (D0)
      Uno RX (D0) -> Nano TX (D1)
      GND <-> GND (common ground, required)

  NOTE: Because this link uses the hardware Serial pins (D0/D1),
  disconnect the cross-wiring to the NANO (or unplug the Uno's
  USB) while uploading this sketch.
*/

// ---------- L298N motor driver ----------
const uint8_t IN1 = 4; // right motor forward
const uint8_t IN2 = 5; // right motor reverse
const uint8_t IN3 = 6; // left motor forward
const uint8_t IN4 = 7; // left motor reverse
const uint8_t LEFT_ENA = 3; // left motor speed (PWM)

// ---------- Left ultrasonic sensor (wall-following) ----------
const uint8_t LEFT_TRIG = 8;
const uint8_t LEFT_ECHO = 9;

// ---------- Tunable (cm) ----------
const int LEFT_WALL_CM = 20; // left sensor reading at or below this = "wall detected"

// ---------- Left motor speed (PWM, 0-255) ----------
const uint8_t LEFT_FULL_SPEED = 255;
const uint8_t LEFT_HALF_SPEED = 75; // used instead of fully stopping the left wheel while curving

// ---------- Obstacle avoidance manoeuvre timing ----------
const unsigned long REVERSE_MS = 300;        // brief back-up before turning
const unsigned long SHARP_TURN_MS = 400;     // duration of each tank turn (tune this)
const unsigned long CLEAR_FORWARD_MS = 750;  // 0.75s forward burst, run twice

const unsigned long START_DELAY_MS = 3000; // time to place the robot before it moves

// ---------- Data from the Nano ----------
String colour = "NONE";

void setup() {
  Serial.begin(9600); // link to Nano

  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);
  pinMode(LEFT_ENA, OUTPUT);

  pinMode(LEFT_TRIG, OUTPUT);
  pinMode(LEFT_ECHO, INPUT);

  stopMotors();
  delay(START_DELAY_MS);
}

void loop() {
  readNanoData();

  if (colour == "RED") {
    avoidObstacle(true);  // red -> turn left first
    return;
  }
  if (colour == "GREEN") {
    avoidObstacle(false); // green -> turn right first
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
// Serial: the Nano sends just the colour classification,
// one line per loop (e.g. "RED", "GREEN", "BLUE", "NONE").
// ---------------------------------------------------
void readNanoData() {
  static String buffer = "";

  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') {
      buffer.trim();
      if (buffer.length() > 0) colour = buffer;
      buffer = "";
    } else if (c != '\r') {
      buffer += c;
    }
  }
}

// ---------------------------------------------------
// Stop, back up, sharp turn, forward, sharp turn back, forward -
// ends up on the other side of the obstacle at the same distance
// from the wall it started at.
// ---------------------------------------------------
void avoidObstacle(bool turnLeftFirst) {
  stopMotors();

  moveBackward();
  delay(REVERSE_MS);

  // Turn away and drive clear - this pair must fully get the
  // obstacle behind the car by itself.
  if (turnLeftFirst) tankTurnLeft(); else tankTurnRight();
  delay(SHARP_TURN_MS);

  bothForward();
  delay(CLEAR_FORWARD_MS);

  // Turn back and drive forward again - purely to close the
  // gap back to the wall now that the obstacle is already
  // behind the car; unrelated to clearing the obstacle itself.
  if (turnLeftFirst) tankTurnRight(); else tankTurnLeft(); // opposite turn, same duration
  delay(SHARP_TURN_MS);

  bothForward();
  delay(CLEAR_FORWARD_MS);
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

void curveTowardLeftWall() {
  // Left wheel (IN3/IN4) kept forward but slowed to half speed via
  // ENA, right wheel (IN1/IN2) stays at full speed - the robot arcs
  // left until the left ultrasonic finds the wall again.
  analogWrite(LEFT_ENA, LEFT_HALF_SPEED);
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
  // Right wheel forward, left wheel backward - spins sharply left.
  analogWrite(LEFT_ENA, LEFT_FULL_SPEED);
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, HIGH);
}

void tankTurnRight() {
  // Left wheel forward, right wheel backward - spins sharply right.
  analogWrite(LEFT_ENA, LEFT_FULL_SPEED);
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, HIGH);
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
