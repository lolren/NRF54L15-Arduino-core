/*
  RGB PWM sample using D6/D7/D8.
  Wire these pins to an RGB LED through resistors.
*/

#ifndef PIN_R
#define PIN_R PIN_D6
#endif

#ifndef PIN_G
#define PIN_G PIN_D7
#endif

#ifndef PIN_B
#define PIN_B PIN_D8
#endif

static void setRgb(uint8_t r, uint8_t g, uint8_t b) {
  analogWrite(PIN_R, r);
  analogWrite(PIN_G, g);
  analogWrite(PIN_B, b);
}

void setup() {
  pinMode(PIN_R, OUTPUT);
  pinMode(PIN_G, OUTPUT);
  pinMode(PIN_B, OUTPUT);
}

void loop() {
  setRgb(255, 0, 0);
  delay(400);
  setRgb(0, 255, 0);
  delay(400);
  setRgb(0, 0, 255);
  delay(400);
  setRgb(255, 180, 0);
  delay(400);
  setRgb(0, 180, 255);
  delay(400);
  setRgb(0, 0, 0);
  delay(200);
}
