// Joystick + OLED hardware test.
//
// Wiring (see hardware.md):
//   OLED  GND->GND  VCC->3V3  SCL->GPIO22  SDA->GPIO21
//   Stick GND->GND  +5V->3V3  VRX->GPIO32  VRY->GPIO33  SW->GPIO25
//   Key   one pin->GPIO26, other pin->GND (mechanical keyboard switch)
#include <Arduino.h>
#include <U8g2lib.h>
#include <Wire.h>

constexpr int PIN_SDA = 21;
constexpr int PIN_SCL = 22;
constexpr int PIN_VRX = 32;  // ADC1
constexpr int PIN_VRY = 33;  // ADC1
constexpr int PIN_SW = 25;   // pressed = LOW, needs the internal pull-up
constexpr int PIN_KEY = 26;  // keyboard switch, pressed = LOW

#ifdef OLED_SH1106
U8G2_SH1106_128X64_NONAME_F_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE);
#else
U8G2_SSD1306_128X64_NONAME_F_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE);
#endif

void scanI2C() {
  Serial.println("I2C scan:");
  int found = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("  device at 0x%02X\n", addr);
      found++;
    }
  }
  if (found == 0) Serial.println("  nothing found: check SDA/SCL/VCC/GND");
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\njoystick-oled test starting");

  pinMode(PIN_SW, INPUT_PULLUP);
  pinMode(PIN_KEY, INPUT_PULLUP);
  analogReadResolution(12);  // 0..4095

  Wire.begin(PIN_SDA, PIN_SCL);
  scanI2C();

  oled.begin();
  oled.setFont(u8g2_font_6x10_tf);
}

void loop() {
  static uint32_t lastPrint = 0;
  static uint32_t lastDraw = 0;
  uint32_t now = millis();
  if (now - lastDraw < 33) return;  // ~30 fps
  lastDraw = now;

  int x = 4095 - analogRead(PIN_VRX);  // inverted so stick-right moves the dot right
  int y = 4095 - analogRead(PIN_VRY);  // inverted so stick-up moves the dot up
  bool pressed = digitalRead(PIN_SW) == LOW;
  // Sampling every 33 ms is slow enough to ride out switch bounce.
  static bool keyWasDown = false;
  static int keyPresses = 0;
  bool keyDown = digitalRead(PIN_KEY) == LOW;
  if (keyDown && !keyWasDown) keyPresses++;
  keyWasDown = keyDown;

  oled.clearBuffer();
  oled.drawFrame(0, 0, 64, 64);
  int dx = map(x, 0, 4095, 2, 61);
  int dy = map(y, 0, 4095, 2, 61);
  if (pressed) oled.drawDisc(dx, dy, 4);
  else oled.drawCircle(dx, dy, 4);
  oled.setCursor(70, 14);
  oled.printf("X %4d", x);
  oled.setCursor(70, 28);
  oled.printf("Y %4d", y);
  oled.setCursor(70, 42);
  oled.print(pressed ? "SW down" : "SW up");
  oled.setCursor(70, 56);
  oled.printf("%s %d", keyDown ? "KEY" : "key", keyPresses);
  oled.sendBuffer();

  static int minX = 4095, maxX = 0, minY = 4095, maxY = 0;
  minX = min(minX, x); maxX = max(maxX, x);
  minY = min(minY, y); maxY = max(maxY, y);

  if (now - lastPrint >= 200) {
    lastPrint = now;
    Serial.printf("x=%4d y=%4d sw=%s key=%s presses=%d  | seen x %d..%d  y %d..%d\n",
                  x, y, pressed ? "DOWN" : "up", keyDown ? "DOWN" : "up",
                  keyPresses, minX, maxX, minY, maxY);
  }
}
