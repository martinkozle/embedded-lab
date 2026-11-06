#include <Arduino.h>

#ifndef LED_PIN
#define LED_PIN 2
#endif

// Morse timing: dot = 1 unit, dash = 3 units, 1 unit between symbols,
// 3 units between letters, 7 units between words.
constexpr uint32_t UNIT_MS = 200;

static void symbol(uint32_t units) {
  digitalWrite(LED_PIN, HIGH);
  delay(units * UNIT_MS);
  digitalWrite(LED_PIN, LOW);
  delay(UNIT_MS);
}

static void letter(uint32_t units) {
  for (int i = 0; i < 3; i++) symbol(units);
  delay(2 * UNIT_MS);
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  Serial.println("blink-sos: started");
}

void loop() {
  Serial.println("SOS");
  letter(1);  // S
  letter(3);  // O
  letter(1);  // S
  delay(4 * UNIT_MS);
}
