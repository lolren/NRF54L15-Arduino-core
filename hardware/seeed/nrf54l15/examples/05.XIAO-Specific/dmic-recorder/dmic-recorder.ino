#include <Arduino.h>
#include <PDM.h>

/*
  Serial PCM recorder for quick capture tests.
  Open Serial Monitor/Plotter at 115200 and log output.
*/

static int16_t g_samples[64];

void setup() {
  Serial.begin(115200);
  delay(1200);
  Serial.println("XIAO nRF54L15 DMIC recorder sample");

  if (!PDM.begin(16000, 1)) {
    Serial.println("PDM begin failed");
    return;
  }

  Serial.println("Streaming 16-bit PCM CSV lines...");
}

void loop() {
  const int bytes = PDM.read(g_samples, sizeof(g_samples));
  if (bytes <= 0) {
    return;
  }

  const int count = bytes / static_cast<int>(sizeof(int16_t));
  for (int i = 0; i < count; ++i) {
    Serial.println(g_samples[i]);
  }
}
