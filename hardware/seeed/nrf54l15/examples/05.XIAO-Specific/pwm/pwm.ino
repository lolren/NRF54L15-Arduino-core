/*
  PWM fade on XIAO nRF54L15.
  Uses D6 (PWM-capable in this core).
*/

#ifndef PWM_PIN
#define PWM_PIN PIN_D6
#endif

void setup() {
  pinMode(PWM_PIN, OUTPUT);
}

void loop() {
  for (int v = 0; v <= 255; ++v) {
    analogWrite(PWM_PIN, v);
    delay(4);
  }

  for (int v = 255; v >= 0; --v) {
    analogWrite(PWM_PIN, v);
    delay(4);
  }
}
