#include <Arduino.h>
#include <Bluetooth.h>

static bool g_bleReady = false;
static bool g_advRunning = false;
static unsigned long g_lastRetryMs = 0;

static void tryStartAdvertising() {
  g_advRunning = BLE.advertise();
  if (g_advRunning) {
    Serial.println("BLE advertising started");
  } else {
    Serial.print("BLE advertising failed, err=");
    Serial.println(BLE.lastError());
  }
}

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);

  Serial.begin(115200);
  delay(1000);

  g_bleReady = BLE.begin("XIAO-nRF54L15");
  if (!g_bleReady) {
    Serial.println("BLE init failed");
    Serial.print("err=");
    Serial.println(BLE.lastError());
    return;
  }

  tryStartAdvertising();
}

void loop() {
  if (g_bleReady && !g_advRunning) {
    unsigned long now = millis();
    if (now - g_lastRetryMs >= 2000UL) {
      g_lastRetryMs = now;
      tryStartAdvertising();
    }
  }

  digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
  delay(250);
}
