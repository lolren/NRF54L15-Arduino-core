/*
  XIAO nRF54L15 board test: Serial <-> Serial1 bridge + Serial1 loopback.

  Wiring for loopback test:
  - D6 (Serial1 TX) -> D7 (Serial1 RX)

  Use Serial Monitor at 115200 baud.
  - Characters typed in Serial Monitor are forwarded to Serial1.
  - Data received on Serial1 is printed back to Serial Monitor.
*/

static unsigned long g_lastPingMs = 0;
static uint32_t g_counter = 0;

void setup() {
  Serial.begin(115200);
  Serial1.begin(115200);
  delay(1000);

  Serial.println("XIAO nRF54L15 Serial1 loopback test");
  Serial.println("Connect D6->D7 for physical loopback.");
}

void loop() {
  while (Serial.available() > 0) {
    Serial1.write(static_cast<uint8_t>(Serial.read()));
  }

  while (Serial1.available() > 0) {
    Serial.write(static_cast<uint8_t>(Serial1.read()));
  }

  const unsigned long now = millis();
  if (now - g_lastPingMs >= 1000UL) {
    g_lastPingMs = now;
    Serial1.print("ping ");
    Serial1.println(g_counter++);
  }
}

