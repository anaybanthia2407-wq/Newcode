/*
  Arduino UNO - Main Controller
  ---------------------------------------------------
  Line-following + obstacle-avoidance robot.

  Receives IR line-sensor data + colour data from the
  Arduino NANO over the hardware serial line (D0/D1),
  reads two HC-SR04 ultrasonic sensors, pans two servos,
  and drives an L298N motor driver.

  Wiring (Arduino Uno):
    Ultrasonic Sensor 1 (front):
      TRIG -> D2
      ECHO -> D13

    Ultrasonic Sensor 2 (side):
      TRIG -> D12
      ECHO -> D10

    Servo 1 (pans Ultrasonic Sensor 1) -> D11
    Servo 2 (pans Ultrasonic Sensor 2) -> D9

    L298N Motor Driver (direction pins):
      IN1 -> D7   (Motor A / left, forward)
      IN2 -> D6   (Motor A / left, reverse)
      IN3 -> D5   (Motor B / right, forward)
      IN4 -> D4   (Motor B / right, reverse)
      ENA / ENB   -> tied directly to 5V (full-speed only,
                     no PWM speed control pins were wired)

    Serial link to NANO:
      Uno TX (D1) -> Nano RX (D0)
      Uno RX (D0) -> Nano TX (D1)
      GND <-> GND (common ground, required)

  NOTE: Because this link uses the hardware Serial pins
  (D0/D1), disconnect the cross-wiring to the NANO (or
  unplug the Uno's USB) while uploading this sketch,
  otherwise the upload can fail/garble.
*/

#include <Servo.h>

// ---------- Ultrasonic sensors ----------
const uint8_t TRIG1 = 2;
const uint8_t ECHO1 = 13;
const uint8_t TRIG2 = 12;
const uint8_t ECHO2 = 10;

// ---------- Servos ----------
const uint8_t SERVO1_PIN = 11;
const uint8_t SERVO2_PIN = 9;
Servo servo1;
Servo servo2;

// ---------- L298N motor driver ----------
const uint8_t IN1 = 7;
const uint8_t IN2 = 6;
const uint8_t IN3 = 5;
const uint8_t IN4 = 4;

// ---------- Tunables ----------
const int OBSTACLE_DISTANCE_CM = 15;   // stop/avoid if closer than this
const bool LINE_IS_LOW = true;         // IR sensor reports LOW when it sees the black line; flip to false if your modules are active-HIGH

// ---------- Data received from the Nano ----------
int irValue[5] = {1, 1, 1, 1, 1}; // 1 = no line, 0 = line, per LINE_IS_LOW convention
String colour = "NONE";

void setup() {
  Serial.begin(9600); // link to Nano

  pinMode(TRIG1, OUTPUT);
  pinMode(ECHO1, INPUT);
  pinMode(TRIG2, OUTPUT);
  pinMode(ECHO2, INPUT);

  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);

  servo1.attach(SERVO1_PIN);
  servo2.attach(SERVO2_PIN);
  servo1.write(90); // centre
  servo2.write(90); // centre

  stopMotors();
}

void loop() {
  readNanoData();

  long frontDistance = readDistanceCm(TRIG1, ECHO1);

  if (colour == "RED") {
    // Example colour-triggered behaviour: treat red as a stop signal.
    // Customize this block for whatever colour logic you need.
    stopMotors();
    return;
  }

  if (frontDistance > 0 && frontDistance < OBSTACLE_DISTANCE_CM) {
    avoidObstacle();
  } else {
    followLine();
  }

  delay(20);
}

// ---------------------------------------------------
// Serial: parse "ir1,ir2,ir3,ir4,ir5,COLOUR" from Nano
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
  int idx[5];
  int start = 0;
  int field = 0;

  for (int i = 0; i < frame.length() && field < 5; i++) {
    if (frame.charAt(i) == ',') {
      irValue[field] = frame.substring(start, i).toInt();
      start = i + 1;
      field++;
    }
  }
  if (field == 5) {
    colour = frame.substring(start);
    colour.trim();
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
// Line following using the 5-way IR array from the Nano
// ---------------------------------------------------
void followLine() {
  bool l2 = isLine(irValue[0]); // far left
  bool l1 = isLine(irValue[1]); // left
  bool c  = isLine(irValue[2]); // centre
  bool r1 = isLine(irValue[3]); // right
  bool r2 = isLine(irValue[4]); // far right

  if (c && !l1 && !r1) {
    moveForward();
  } else if (l1 || l2) {
    turnLeft();
  } else if (r1 || r2) {
    turnRight();
  } else {
    // No sensor sees the line: stop and wait rather than drive blind.
    stopMotors();
  }
}

bool isLine(int rawValue) {
  return LINE_IS_LOW ? (rawValue == LOW) : (rawValue == HIGH);
}

// ---------------------------------------------------
// Obstacle avoidance: stop, scan with the servo-mounted
// ultrasonic sensors, then turn toward the clearer side.
// ---------------------------------------------------
void avoidObstacle() {
  stopMotors();
  delay(150);

  servo1.write(150); // scan left
  delay(300);
  long leftDistance = readDistanceCm(TRIG1, ECHO1);

  servo1.write(30); // scan right
  delay(300);
  long rightDistance = readDistanceCm(TRIG1, ECHO1);

  servo1.write(90); // recentre
  delay(200);

  if (leftDistance == 0 || leftDistance > rightDistance) {
    turnLeft();
  } else {
    turnRight();
  }
  delay(300);
  stopMotors();
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

void moveBackward() {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, HIGH);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, HIGH);
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
