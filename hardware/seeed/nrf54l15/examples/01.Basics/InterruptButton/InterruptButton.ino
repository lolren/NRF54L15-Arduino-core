#include <Arduino.h>

volatile uint32_t g_count = 0;

void onButton() {
  g_count++;
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_BUTTON), onButton, FALLING);
}

void loop() {
  static uint32_t last = 0;
  if (last != g_count) {
    last = g_count;
    Serial.print("button interrupts: ");
    Serial.println(last);
  }
  delay(50);
}
