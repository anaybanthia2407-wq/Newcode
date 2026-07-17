/*
  Arduino UNO - RUSH Robo Race Main Controller (Round 1: Qualifier)
  ---------------------------------------------------
  The right ultrasonic sensor is still unused here. The Nano's
  5-channel IR array (now angled down at the ground, close to the
  surface, detecting the black boundary tape) is back in play as a
  safety backstop on top of the normal wall-hugging:

    boundary tape detected by ANY IR channel -> override everything
    and pivot sharply back toward the wall (the ultrasonic-based
    hugging let the robot drift too close to the outer edge)
    otherwise -> normal Round 1 wall-following:
      left ultrasonic wall detected within range -> both wheels
      forward at full speed
      wall not detected (too far / lost it) -> keep the LEFT wheel
      driving forward but at half speed (via its ENA PWM pin) while
      the RIGHT wheel stays at full speed, curving the robot back
      left until the wall is picked up again

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

    Serial link to NANO (5-channel IR boundary array):
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

// ---------- Left ultrasonic sensor ----------
const uint8_t LEFT_TRIG = 8;
const uint8_t LEFT_ECHO = 9;

// ---------- Tunable (cm) ----------
const int LEFT_WALL_CM = 20; // left sensor reading at or below this = "wall detected"

// ---------- Left motor speed (PWM, 0-255) ----------
const uint8_t LEFT_FULL_SPEED = 255;
const uint8_t LEFT_HALF_SPEED = 75; // used instead of fully stopping the left wheel while curving

// ---------- IR boundary sensor polarity ----------
const bool IR_ACTIVE_HIGH = true; // set false if your IR module outputs LOW when it senses the black tape

// ---------- Boundary correction timing ----------
const unsigned long BOUNDARY_TURN_MS = 300; // how long to pivot back toward the wall once tape is seen

const unsigned long START_DELAY_MS = 3000; // time to place the robot before it moves

// ---------- Data from the Nano ----------
int irRight2Left[5] = {0, 0, 0, 0, 0}; // s1 (extreme right) .. s5 (extreme left)

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

  if (isBoundaryDetected()) {
    turnLeftPivot(); // sharp correction back toward the wall
    delay(BOUNDARY_TURN_MS);
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
// Serial: parse "s1,s2,s3,s4,s5" from the Nano
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

  for (int i = 0; i < frame.length() && field < 4; i++) {
    if (frame.charAt(i) == ',') {
      irRight2Left[field] = frame.substring(start, i).toInt();
      start = i + 1;
      field++;
    }
  }
  if (field == 4) {
    irRight2Left[4] = frame.substring(start).toInt();
  }
}

bool isBoundaryDetected() {
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
  // Right wheel stopped, left wheel forward at full speed - a
  // sharper correction than curveTowardLeftWall(), used when the
  // IR array says the robot has reached the boundary tape.
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
