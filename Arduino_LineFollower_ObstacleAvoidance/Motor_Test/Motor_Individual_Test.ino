/*
  Arduino UNO - Individual Motor Test
  ---------------------------------------------------
  Tests Motor A and Motor B one at a time (forward, then
  backward), so each motor/wire can be diagnosed on its
  own instead of only seeing combined turning behaviour.
  Status is printed to the Serial Monitor (9600 baud).

  Wiring (Arduino Uno -> L298N):
    IN1 -> D7   (Motor A forward)
    IN2 -> D6   (Motor A backward)
    IN3 -> D5   (Motor B forward)
    IN4 -> D4   (Motor B backward)
    ENA / ENB   -> tied directly to 5V (full-speed only,
                   no PWM speed control pins were wired)

  NOTE: This sketch only uses Serial for USB debug prints
  (D0/D1 are free here since the Nano isn't involved in
  this test) - safe to run with the Nano disconnected.
*/

const uint8_t IN1 = 7; // Motor A forward
const uint8_t IN2 = 6; // Motor A backward
const uint8_t IN3 = 5; // Motor B forward
const uint8_t IN4 = 4; // Motor B backward

const unsigned long RUN_TIME_MS = 1500;
const unsigned long STOP_TIME_MS = 500;

void setup() {
  Serial.begin(9600);

  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);

  stopMotorA();
  stopMotorB();
  Serial.println("Individual motor test starting...");
}

void loop() {
  Serial.println("Motor A - FORWARD");
  motorA(true);
  delay(RUN_TIME_MS);
  stopMotorA();
  delay(STOP_TIME_MS);

  Serial.println("Motor A - BACKWARD");
  motorA(false);
  delay(RUN_TIME_MS);
  stopMotorA();
  delay(STOP_TIME_MS);

  Serial.println("Motor B - FORWARD");
  motorB(true);
  delay(RUN_TIME_MS);
  stopMotorB();
  delay(STOP_TIME_MS);

  Serial.println("Motor B - BACKWARD");
  motorB(false);
  delay(RUN_TIME_MS);
  stopMotorB();
  delay(STOP_TIME_MS);
}

void motorA(bool forward) {
  digitalWrite(IN1, forward ? HIGH : LOW);
  digitalWrite(IN2, forward ? LOW : HIGH);
}

void stopMotorA() {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
}

void motorB(bool forward) {
  digitalWrite(IN3, forward ? HIGH : LOW);
  digitalWrite(IN4, forward ? LOW : HIGH);
}

void stopMotorB() {
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, LOW);
}
