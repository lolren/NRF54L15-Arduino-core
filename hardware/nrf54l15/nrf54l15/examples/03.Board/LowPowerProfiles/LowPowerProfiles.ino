/*
 * LowPowerProfiles
 *
 * Board-level power profile demo with runtime profile + peripheral gating.
 * Press USER button to cycle profile:
 *   0 = Performance (128 MHz + peripherals enabled)
 *   1 = Balanced (64 MHz + peripherals enabled)
 *   2 = Ultra-Low-Power (64 MHz + non-essential peripherals suspended)
 */

#include <XiaoNrf54L15.h>

struct ProfileEntry {
  XiaoPowerProfile profile;
  const char* name;
  uint16_t heartbeatSleepMs;
};

static const ProfileEntry kProfiles[] = {
  {XIAO_POWER_PROFILE_PERFORMANCE, "Performance", 250},
  {XIAO_POWER_PROFILE_BALANCED, "Balanced", 750},
  {XIAO_POWER_PROFILE_ULTRA_LOW_POWER, "UltraLowPower", 2000},
};

static uint8_t g_profileIndex = 0;
static bool g_buttonWasDown = false;
static uint32_t g_cycle = 0;

static const char* onOff(bool v)
{
  return v ? "on" : "off";
}

static void printPeripheralState()
{
  Serial.print(" periph-uart0=");
  Serial.print(onOff(XiaoNrf54L15.peripheralEnabled(XIAO_PERIPHERAL_UART0)));
  Serial.print(" uart1=");
  Serial.print(onOff(XiaoNrf54L15.peripheralEnabled(XIAO_PERIPHERAL_UART1)));
  Serial.print(" i2c=");
  Serial.print(onOff(XiaoNrf54L15.peripheralEnabled(XIAO_PERIPHERAL_I2C0)));
  Serial.print(" spi=");
  Serial.print(onOff(XiaoNrf54L15.peripheralEnabled(XIAO_PERIPHERAL_SPI0)));
  Serial.print(" adc=");
  Serial.print(onOff(XiaoNrf54L15.peripheralEnabled(XIAO_PERIPHERAL_ADC)));
  Serial.print(" pwm=");
  Serial.println(onOff(XiaoNrf54L15.peripheralEnabled(XIAO_PERIPHERAL_PWM0)));
}

static void applyAndPrintProfile()
{
  const ProfileEntry& p = kProfiles[g_profileIndex];
  Serial.print("profile=");
  Serial.print(p.name);
  const bool applied = XiaoNrf54L15.applyPowerProfile(p.profile);
  Serial.print(" apply=");
  Serial.print(applied ? "ok" : "fail");
  Serial.print(" cpu-hz=");
  Serial.print(XiaoNrf54L15.cpuFrequencyHz());
  Serial.print(" err=");
  Serial.println(XiaoNrf54L15.peripheralLastError());
  printPeripheralState();
}

void setup()
{
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(PIN_BUTTON, INPUT_PULLUP);

  Serial.begin(115200);
  delay(250);
  Serial.println("low-power-profiles: ready");
  Serial.println("press USER button to cycle profile");
  applyAndPrintProfile();
}

void loop()
{
  const bool buttonDown = (digitalRead(PIN_BUTTON) == LOW);
  if (buttonDown && !g_buttonWasDown) {
    g_profileIndex = static_cast<uint8_t>((g_profileIndex + 1U) % (sizeof(kProfiles) / sizeof(kProfiles[0])));
    applyAndPrintProfile();
  }
  g_buttonWasDown = buttonDown;

  const ProfileEntry& p = kProfiles[g_profileIndex];

  digitalWrite(LED_BUILTIN, LOW);
  delay(20);
  digitalWrite(LED_BUILTIN, HIGH);

  Serial.print("cycle=");
  Serial.print(g_cycle++);
  Serial.print(" uptime-ms=");
  Serial.println(millis());

  XiaoNrf54L15.sleepMs(p.heartbeatSleepMs);
}
