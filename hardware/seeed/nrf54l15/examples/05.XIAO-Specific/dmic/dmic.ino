#include <Arduino.h>
#include <PDM.h>

static int16_t g_samples[128];

void setup() {
  Serial.begin(115200);
  delay(1200);
  Serial.println("XIAO nRF54L15 DMIC sample");

  if (!PDM.begin(16000, 1)) {
    Serial.println("PDM begin failed");
    return;
  }

  Serial.println("PDM started (16 kHz mono)");
}

void loop() {
  const int bytes = PDM.read(g_samples, sizeof(g_samples));
  if (bytes <= 0) {
    delay(20);
    return;
  }

  const int count = bytes / static_cast<int>(sizeof(int16_t));
  int peak = 0;
  long sumAbs = 0;
  for (int i = 0; i < count; ++i) {
    const int v = g_samples[i] >= 0 ? g_samples[i] : -g_samples[i];
    if (v > peak) {
      peak = v;
    }
    sumAbs += v;
  }

  const int avg = count > 0 ? static_cast<int>(sumAbs / count) : 0;
  Serial.print("samples=");
  Serial.print(count);
  Serial.print(" avgAbs=");
  Serial.print(avg);
  Serial.print(" peak=");
  Serial.println(peak);
}
