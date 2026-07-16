/*
  Arduino UNO - RUSH Robo Race Main Controller (Round 2: Semi-Final)
  ---------------------------------------------------
  Builds on the confirmed-working Round 1 left-wall follower and
  adds obstacle avoidance:

    1. Default behaviour is the Round 1 wall follower: left
       ultrasonic hugs the wall, curving via the left motor's ENA
       PWM (half speed) instead of a full stop.
    2. The Nano's forward-facing 5-channel IR array is the obstacle
       trigger - when it detects something ahead, the robot stops
       and sweeps the right servo (which carries the right
       ultrasonic sensor) across a range of angles to pinpoint the
       obstacle's bearing and distance.
    3. It then reads the colour sensor's latest classification
       (also streamed from the Nano):
         RED   -> go around it via a LEFT turn
         GREEN -> go around it via a RIGHT turn
       using a timed turn / forward / turn-back manoeuvre (the
       obstacles are cylinders of radius ~10cm, so OBSTACLE_CLEAR_MS
       should cover roughly that ~20cm diameter plus clearance).
    4. Afterwards control returns to the Round 1 wall follower.

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

    Right Ultrasonic Sensor (mounted on the right servo horn):
      TRIG -> D10
      ECHO -> D11

    Right servo (sweeps the right ultrasonic to pinpoint an
    obstacle once the Nano's IR array detects one):
      Right servo -> A0

    Serial link to NANO (IR array + colour sensor):
      Uno TX (D1) -> Nano RX (D0)
      Uno RX (D0) -> Nano TX (D1)
      GND <-> GND (common ground, required)

  NOTE: Because this link uses the hardware Serial pins (D0/D1),
  disconnect the cross-wiring to the NANO (or unplug the Uno's
  USB) while uploading this sketch.
*/

#include <Servo.h>

// ---------- L298N motor driver ----------
const uint8_t IN1 = 4; // right motor forward
const uint8_t IN2 = 5; // right motor reverse
const uint8_t IN3 = 6; // left motor forward
const uint8_t IN4 = 7; // left motor reverse
const uint8_t LEFT_ENA = 3; // left motor speed (PWM)

// ---------- Left ultrasonic sensor (wall-following) ----------
const uint8_t LEFT_TRIG = 8;
const uint8_t LEFT_ECHO = 9;

// ---------- Right ultrasonic sensor (obstacle pinpointing) ----------
const uint8_t RIGHT_TRIG = 10;
const uint8_t RIGHT_ECHO = 11;

// ---------- Right servo (carries the right ultrasonic sensor) ----------
const uint8_t RIGHT_SERVO_PIN = A0;
Servo rightServo;
const int SERVO_CENTRE_ANGLE = 90;
const int SWEEP_MIN_ANGLE = 30;
const int SWEEP_MAX_ANGLE = 150;
const int SWEEP_STEP_DEG = 15;
const unsigned long SWEEP_SETTLE_MS = 100; // let the servo settle before each reading

// ---------- Tunables (cm) ----------
const int LEFT_WALL_CM = 20; // left sensor reading at or below this = "wall detected"

// ---------- Left motor speed (PWM, 0-255) ----------
const uint8_t LEFT_FULL_SPEED = 255;
const uint8_t LEFT_HALF_SPEED = 75; // used instead of fully stopping the left wheel while curving

// ---------- IR obstacle-detection polarity ----------
const bool IR_ACTIVE_HIGH = true; // set false if your IR module outputs LOW when it senses something close

// ---------- Obstacle avoidance manoeuvre timing (object radius ~10cm) ----------
const unsigned long OBSTACLE_TURN_MS = 400;   // time to turn away from / back toward the wall
const unsigned long OBSTACLE_CLEAR_MS = 500;  // time driving forward to clear the ~20cm-wide obstacle

const unsigned long START_DELAY_MS = 3000; // time to place the robot before it moves

// ---------- Data from the Nano ----------
int irRight2Left[5] = {0, 0, 0, 0, 0}; // s1 (extreme right) .. s5 (extreme left)
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
  pinMode(RIGHT_TRIG, OUTPUT);
  pinMode(RIGHT_ECHO, INPUT);

  rightServo.attach(RIGHT_SERVO_PIN);
  rightServo.write(SERVO_CENTRE_ANGLE);

  stopMotors();
  delay(START_DELAY_MS);
}

void loop() {
  readNanoData();

  if (isObstacleDetected()) {
    handleObstacle();
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

bool isObstacleDetected() {
  for (int i = 0; i < 5; i++) {
    bool triggered = IR_ACTIVE_HIGH ? (irRight2Left[i] == HIGH) : (irRight2Left[i] == LOW);
    if (triggered) return true;
  }
  return false;
}

// ---------------------------------------------------
// Obstacle handling: stop, sweep the right servo/ultrasonic to
// pinpoint the obstacle, then check the Nano's colour reading
// and go around it - left for red, right for green.
// ---------------------------------------------------
void handleObstacle() {
  stopMotors();
  delay(100);

  long obstacleDist;
  locateObstacleAngle(obstacleDist);

  readNanoData(); // grab the freshest colour reading now that we're close to it

  if (colour == "RED") {
    avoidObstacle(true);  // red -> go around via the left
  } else if (colour == "GREEN") {
    avoidObstacle(false); // green -> go around via the right
  } else {
    // Colour unclear - stay stopped rather than guess a direction.
    stopMotors();
  }
}

// Sweeps the right servo across SWEEP_MIN_ANGLE..SWEEP_MAX_ANGLE,
// taking a right-ultrasonic reading at each step, and returns the
// angle with the closest valid reading (the obstacle's bearing).
// Recentres the servo afterward. outDist is set to that closest
// distance (0 if nothing was seen anywhere in the sweep).
int locateObstacleAngle(long &outDist) {
  int bestAngle = SERVO_CENTRE_ANGLE;
  long bestDist = 0;

  for (int angle = SWEEP_MIN_ANGLE; angle <= SWEEP_MAX_ANGLE; angle += SWEEP_STEP_DEG) {
    rightServo.write(angle);
    delay(SWEEP_SETTLE_MS);
    long d = readDistanceCm(RIGHT_TRIG, RIGHT_ECHO);
    if (d > 0 && (bestDist == 0 || d < bestDist)) {
      bestDist = d;
      bestAngle = angle;
    }
  }

  rightServo.write(SERVO_CENTRE_ANGLE);
  delay(SWEEP_SETTLE_MS);

  outDist = bestDist;
  return bestAngle;
}

// ---------------------------------------------------
// Timed turn / forward / turn-back manoeuvre to go around an
// obstacle, then hand control back to the wall follower.
// ---------------------------------------------------
void avoidObstacle(bool turnLeftFirst) {
  if (turnLeftFirst) turnLeftPivot(); else turnRightPivot();
  delay(OBSTACLE_TURN_MS);

  bothForward();
  delay(OBSTACLE_CLEAR_MS);

  if (turnLeftFirst) turnRightPivot(); else turnLeftPivot();
  delay(OBSTACLE_TURN_MS);

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

void turnLeftPivot() {
  // Right wheel stopped, left wheel forward at full speed - pivots left.
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  analogWrite(LEFT_ENA, LEFT_FULL_SPEED);
  digitalWrite(IN3, HIGH);
  digitalWrite(IN4, LOW);
}

void turnRightPivot() {
  // Left wheel stopped, right wheel forward at full speed - pivots right.
  analogWrite(LEFT_ENA, 0);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, LOW);
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);
}

void stopMotors() {
  analogWrite(LEFT_ENA, LEFT_FULL_SPEED);
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, LOW);
}
