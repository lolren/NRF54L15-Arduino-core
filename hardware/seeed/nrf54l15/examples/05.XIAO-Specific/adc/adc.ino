/*
  XIAO nRF54L15 board-specific ADC sample.
  Reads A0..A3 and prints raw values.
*/

void setup() {
  Serial.begin(115200);
  delay(1000);
  analogReadResolution(12);
  Serial.println("XIAO nRF54L15 ADC sample");
}

void loop() {
  const int a0 = analogRead(A0);
  const int a1 = analogRead(A1);
  const int a2 = analogRead(A2);
  const int a3 = analogRead(A3);

  Serial.print("A0=");
  Serial.print(a0);
  Serial.print(" A1=");
  Serial.print(a1);
  Serial.print(" A2=");
  Serial.print(a2);
  Serial.print(" A3=");
  Serial.println(a3);
  delay(200);
}
