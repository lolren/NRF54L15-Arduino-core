/*
 * ADCTouchMonitor
 *
 * Board-specific analog monitor for XIAO nRF54L15.
 * Prints A0-A3 values continuously for quick touch/noise checks.
 */

void setup() {
  Serial.begin(115200);
  delay(300);

  analogReadResolution(12);
  pinMode(A0, INPUT);
  pinMode(A1, INPUT);
  pinMode(A2, INPUT);
  pinMode(A3, INPUT);

  Serial.println("adc-touch: ready");
  Serial.println("cols: A0 A1 A2 A3");
}

void loop() {
  int a0 = analogRead(A0);
  int a1 = analogRead(A1);
  int a2 = analogRead(A2);
  int a3 = analogRead(A3);

  Serial.print(a0);
  Serial.print(' ');
  Serial.print(a1);
  Serial.print(' ');
  Serial.print(a2);
  Serial.print(' ');
  Serial.println(a3);

  delay(120);
}
