/*
 * PeripheralPowerGating
 *
 * Demonstrates manual peripheral suspend/resume for current measurements.
 * Type a command over Serial:
 *   on  <uart0|uart1|i2c|spi|adc|pwm>
 *   off <uart0|uart1|i2c|spi|adc|pwm>
 *   status
 */

#include <XiaoNrf54L15.h>

#include <string.h>

static char g_line[96] = {0};
static size_t g_lineLen = 0U;

static const char* onOff(bool v)
{
  return v ? "on" : "off";
}

static bool parsePeripheral(const char* token, XiaoPeripheral* out)
{
  if (token == nullptr || out == nullptr) {
    return false;
  }

  if (strcmp(token, "uart0") == 0) {
    *out = XIAO_PERIPHERAL_UART0;
    return true;
  }
  if (strcmp(token, "uart1") == 0) {
    *out = XIAO_PERIPHERAL_UART1;
    return true;
  }
  if (strcmp(token, "i2c") == 0) {
    *out = XIAO_PERIPHERAL_I2C0;
    return true;
  }
  if (strcmp(token, "spi") == 0) {
    *out = XIAO_PERIPHERAL_SPI0;
    return true;
  }
  if (strcmp(token, "adc") == 0) {
    *out = XIAO_PERIPHERAL_ADC;
    return true;
  }
  if (strcmp(token, "pwm") == 0) {
    *out = XIAO_PERIPHERAL_PWM0;
    return true;
  }

  return false;
}

static void printStatus()
{
  Serial.print("cpu-hz=");
  Serial.print(XiaoNrf54L15.cpuFrequencyHz());
  Serial.print(" profile=");
  Serial.println(static_cast<int>(XiaoNrf54L15.powerProfile()));

  Serial.print("uart0=");
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

static void runCommand(const char* cmd)
{
  if (cmd == nullptr || cmd[0] == '\0') {
    return;
  }

  if (strcmp(cmd, "status") == 0) {
    printStatus();
    return;
  }

  char local[96] = {0};
  strncpy(local, cmd, sizeof(local) - 1);
  local[sizeof(local) - 1] = '\0';

  char* save = nullptr;
  char* action = strtok_r(local, " ", &save);
  char* target = strtok_r(nullptr, " ", &save);
  if (action == nullptr || target == nullptr) {
    Serial.println("invalid command");
    return;
  }

  XiaoPeripheral peripheral;
  if (!parsePeripheral(target, &peripheral)) {
    Serial.println("unknown peripheral");
    return;
  }

  bool enable = false;
  if (strcmp(action, "on") == 0) {
    enable = true;
  } else if (strcmp(action, "off") == 0) {
    enable = false;
  } else {
    Serial.println("unknown action");
    return;
  }

  const bool ok = XiaoNrf54L15.setPeripheralEnabled(peripheral, enable);
  Serial.print(action);
  Serial.print(" ");
  Serial.print(target);
  Serial.print(" -> ");
  Serial.print(ok ? "ok" : "fail");
  Serial.print(" err=");
  Serial.println(XiaoNrf54L15.peripheralLastError());
  printStatus();
}

void setup()
{
  pinMode(LED_BUILTIN, OUTPUT);
  Serial.begin(115200);
  delay(300);

  Serial.println("peripheral-power-gating: ready");
  Serial.println("commands: on/off <uart0|uart1|i2c|spi|adc|pwm>, status");
  printStatus();
}

void loop()
{
  while (Serial.available() > 0) {
    const int c = Serial.read();
    if (c == '\r') {
      continue;
    }

    if (c == '\n') {
      g_line[g_lineLen] = '\0';
      runCommand(g_line);
      g_lineLen = 0U;
      g_line[0] = '\0';
      continue;
    }

    if (g_lineLen < (sizeof(g_line) - 1U)) {
      g_line[g_lineLen++] = static_cast<char>(c);
      g_line[g_lineLen] = '\0';
    }
  }

  digitalWrite(LED_BUILTIN, LOW);
  delay(10);
  digitalWrite(LED_BUILTIN, HIGH);
  XiaoNrf54L15.sleepMs(990);
}
