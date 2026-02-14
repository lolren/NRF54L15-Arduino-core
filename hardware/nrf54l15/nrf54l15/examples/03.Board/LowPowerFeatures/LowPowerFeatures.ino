/*
 * LowPowerFeatures
 *
 * Demonstrates low-power helper APIs for XIAO nRF54L15:
 * - sleepMs/sleepUs wrappers
 * - optional system-off entry via user button
 */

#include <XiaoNrf54L15.h>

static uint32_t g_tick = 0;

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(PIN_BUTTON, INPUT_PULLUP);

  Serial.begin(115200);
  delay(300);

  Serial.println("low-power: ready");
  Serial.print("radio-profile: ");
  Serial.println(XiaoNrf54L15.radioProfileName());
  Serial.print("ble6-requested: ");
  Serial.println(XiaoNrf54L15.ble6FeatureSetRequested() ? "yes" : "no");
  Serial.print("ble6-enabled: ");
  Serial.println(XiaoNrf54L15.ble6FeatureSetEnabled() ? "yes" : "no");
  Serial.print("channel-sounding-enabled: ");
  Serial.println(XiaoNrf54L15.channelSoundingEnabled() ? "enabled" : "disabled");
  Serial.print("tools-f_cpu-hz: ");
  Serial.println(XiaoNrf54L15.cpuFrequencyFromToolsHz());
  Serial.print("active-cpu-hz: ");
  Serial.println(XiaoNrf54L15.cpuFrequencyHz());
#if defined(ENABLE_SYSTEM_OFF_WITH_BUTTON)
  Serial.println("press USER button to enter System OFF");
#else
  Serial.println("system-off demo disabled by default");
  Serial.println("define ENABLE_SYSTEM_OFF_WITH_BUTTON to enable button-triggered System OFF");
#endif
}

void loop() {
  const bool ledOn = (g_tick & 1U) != 0U;
  digitalWrite(LED_BUILTIN, ledOn ? LOW : HIGH);  // active-low LED

  Serial.print("low-power: tick=");
  Serial.print(g_tick);
  Serial.print(" uptime-ms=");
  Serial.println(millis());
  g_tick++;

  #if defined(ENABLE_SYSTEM_OFF_WITH_BUTTON)
  if (digitalRead(PIN_BUTTON) == LOW) {
    Serial.println("low-power: button pressed, requesting System OFF");
    (void)XiaoNrf54L15.requestSystemOff();
    delay(30);
    XiaoNrf54L15.systemOff();
  }
  #endif

  XiaoNrf54L15.sleepMs(1000);
}
