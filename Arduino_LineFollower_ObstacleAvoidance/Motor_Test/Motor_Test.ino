/*
  Arduino UNO - Motor Test
  ---------------------------------------------------
  Standalone sketch to verify the L298N wiring and motor
  direction before running the full line-follower code.
  Cycles: forward -> stop -> backward -> stop -> left ->
  stop -> right -> stop, printing status to the Serial
  Monitor (9600 baud) at each step.

  Wiring (Arduino Uno):
    L298N Motor Driver (direction pins):
      IN1 -> D7   (Motor A / left, forward)
      IN2 -> D6   (Motor A / left, reverse)
      IN3 -> D5   (Motor B / right, forward)
      IN4 -> D4   (Motor B / right, reverse)
      ENA / ENB   -> tied directly to 5V (full-speed only,
                     no PWM speed control pins were wired)

  NOTE: This sketch uses Serial only for USB debug prints
  (D0/D1 are free here since the Nano isn't involved in
  this test) - safe to run with the Nano disconnected.
*/

const uint8_t IN1 = 7;
const uint8_t IN2 = 6;
const uint8_t IN3 = 5;
const uint8_t IN4 = 4;

const unsigned long RUN_TIME_MS = 1500;
const unsigned long STOP_TIME_MS = 800;

void setup() {
  Serial.begin(9600);

  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);

  stopMotors();
  Serial.println("Motor test starting...");
}

void loop() {
  runStep("FORWARD", moveForward);
  runStep("BACKWARD", moveBackward);
  runStep("TURN LEFT", turnLeft);
  runStep("TURN RIGHT", turnRight);
}

void runStep(const char *label, void (*action)()) {
  Serial.println(label);
  action();
  delay(RUN_TIME_MS);

  Serial.println("STOP");
  stopMotors();
  delay(STOP_TIME_MS);
}

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
