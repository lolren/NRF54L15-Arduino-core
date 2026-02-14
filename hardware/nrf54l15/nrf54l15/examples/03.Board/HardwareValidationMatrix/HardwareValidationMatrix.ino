/*
 * HardwareValidationMatrix
 *
 * Unified runtime validation sketch for board bring-up and parity checks.
 * Open Serial Monitor at 115200 and send one-character commands:
 *   h: help/menu
 *   p: print current board/power/radio status
 *   a: ADC A0 sample window
 *   i: I2C bus scan
 *   s: SPI loopback test (wire D10 MOSI -> D9 MISO)
 *   u: UART1 loopback test (wire D6 TX -> D7 RX)
 *   f: toggle CPU frequency (64/128 MHz)
 *   l: cycle power profile (PERFORMANCE/BALANCED/ULTRA_LOW_POWER)
 *   w: watchdog start/feed quick test
 *   z: low-power sleep test (1 second)
 *   b: BLE scan test (BLE profile only)
 *   c: BLE central connect test (BLE profile only)
 *   r: 802.15.4 passive scan test (802.15.4 profile only)
 */

#include <SPI.h>
#include <Wire.h>
#include <XiaoNrf54L15.h>

#if defined(ARDUINO_XIAO_NRF54L15_RADIO_BLE_ONLY) || defined(ARDUINO_XIAO_NRF54L15_RADIO_DUAL)
#define MATRIX_HAS_BLE 1
#include <Bluetooth.h>
#else
#define MATRIX_HAS_BLE 0
#endif

#if defined(ARDUINO_XIAO_NRF54L15_RADIO_802154_ONLY) || defined(ARDUINO_XIAO_NRF54L15_RADIO_DUAL)
#define MATRIX_HAS_802154 1
#include <IEEE802154.h>
#else
#define MATRIX_HAS_802154 0
#endif

#if MATRIX_HAS_BLE
static bool g_bleInitialized = false;
#endif
#if MATRIX_HAS_802154
static bool g_ieeeInitialized = false;
#endif
static bool g_watchdogStarted = false;
static uint32_t g_lastMenuMs = 0;

static const char* onOff(bool v)
{
  return v ? "on" : "off";
}

static const char* powerProfileName(XiaoPowerProfile profile)
{
  switch (profile) {
    case XIAO_POWER_PROFILE_PERFORMANCE:
      return "performance";
    case XIAO_POWER_PROFILE_BALANCED:
      return "balanced";
    case XIAO_POWER_PROFILE_ULTRA_LOW_POWER:
      return "ultra-low-power";
    default:
      return "unknown";
  }
}

static void printMenu()
{
  Serial.println();
  Serial.println("=== Hardware Validation Matrix ===");
  Serial.println("h help/menu   p print status");
  Serial.println("a adc(A0)     i i2c scan");
  Serial.println("s spi loop    u uart1 loop");
  Serial.println("f toggle cpu  l cycle power profile");
  Serial.println("w watchdog    z sleep 1s");
#if MATRIX_HAS_BLE
  Serial.println("b ble scan    c ble central connect");
#else
  Serial.println("b/c ble tests unavailable in selected radio profile");
#endif
#if MATRIX_HAS_802154
  Serial.println("r 802.15.4 passive scan");
#else
  Serial.println("r 802.15.4 test unavailable in selected radio profile");
#endif
  Serial.println("==================================");
}

static void printStatus()
{
  Serial.println("status:");
  Serial.print("  radio-profile: ");
  Serial.println(XiaoNrf54L15.radioProfileName());
  Serial.print("  cpu-tools-hz: ");
  Serial.println(XiaoNrf54L15.cpuFrequencyFromToolsHz());
  Serial.print("  cpu-active-hz: ");
  Serial.println(XiaoNrf54L15.cpuFrequencyHz());
  Serial.print("  power-profile: ");
  Serial.println(powerProfileName(XiaoNrf54L15.powerProfile()));

  Serial.print("  uart0=");
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

  Serial.print("  ble-enabled: ");
  Serial.println(XiaoNrf54L15.bleEnabled() ? "yes" : "no");
  Serial.print("  802154-enabled: ");
  Serial.println(XiaoNrf54L15.ieee802154Enabled() ? "yes" : "no");
}

static void runAdcWindow()
{
  Serial.println("adc: sampling A0 x16 (touch pin to vary value)");

  uint32_t sum = 0;
  int minV = 4095;
  int maxV = 0;
  for (int i = 0; i < 16; ++i) {
    const int v = analogRead(A0);
    if (v < minV) {
      minV = v;
    }
    if (v > maxV) {
      maxV = v;
    }
    sum += static_cast<uint32_t>(v);
    delay(15);
  }

  Serial.print("adc: min=");
  Serial.print(minV);
  Serial.print(" max=");
  Serial.print(maxV);
  Serial.print(" avg=");
  Serial.println(sum / 16U);
}

static void runI2cScan()
{
  Wire.begin();
  int found = 0;
  Serial.println("i2c: scanning 0x01..0x7E");
  for (uint8_t addr = 1; addr < 127; ++addr) {
    Wire.beginTransmission(addr);
    const uint8_t err = Wire.endTransmission();
    if (err == 0) {
      Serial.print("i2c: found 0x");
      if (addr < 16U) {
        Serial.print("0");
      }
      Serial.println(addr, HEX);
      found++;
    }
  }
  Serial.print("i2c: devices=");
  Serial.println(found);
}

static void runSpiLoopback()
{
  SPI.begin();
  SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));

  const uint8_t tx8 = 0xA5;
  const uint8_t rx8 = SPI.transfer(tx8);
  const uint16_t tx16 = 0x5AA5;
  const uint16_t rx16 = SPI.transfer16(tx16);

  SPI.endTransaction();

  Serial.print("spi: tx8=0x");
  Serial.print(tx8, HEX);
  Serial.print(" rx8=0x");
  Serial.print(rx8, HEX);
  Serial.print(" tx16=0x");
  Serial.print(tx16, HEX);
  Serial.print(" rx16=0x");
  Serial.println(rx16, HEX);
  Serial.println((tx8 == rx8 && tx16 == rx16) ? "spi: PASS" : "spi: FAIL (check D10->D9)");
}

static void runUart1Loopback()
{
  Serial1.begin(115200);
  const char* payload = "UART1_LOOPBACK";
  const size_t len = strlen(payload);

  while (Serial1.available() > 0) {
    (void)Serial1.read();
  }

  Serial1.write(reinterpret_cast<const uint8_t*>(payload), len);
  Serial1.flush();

  char rx[32] = {0};
  size_t got = 0;
  const uint32_t deadline = millis() + 200;
  while (millis() < deadline && got < len) {
    while (Serial1.available() > 0 && got < len) {
      const int c = Serial1.read();
      if (c >= 0) {
        rx[got++] = static_cast<char>(c);
      }
    }
  }
  rx[got] = '\0';

  Serial.print("uart1: tx=");
  Serial.print(payload);
  Serial.print(" rx=");
  Serial.println(rx);
  Serial.println((got == len && strcmp(rx, payload) == 0)
                     ? "uart1: PASS"
                     : "uart1: FAIL (check D6->D7)");
}

static void runToggleCpu()
{
  const uint32_t currentHz = XiaoNrf54L15.cpuFrequencyHz();
  const uint32_t targetHz = (currentHz >= 100000000UL) ? 64000000UL : 128000000UL;
  const bool ok = XiaoNrf54L15.setCpuFrequencyHz(targetHz);

  Serial.print("cpu: set ");
  Serial.print(targetHz);
  Serial.print(" -> ");
  Serial.println(ok ? "ok" : "fail");
  Serial.print("cpu: active=");
  Serial.println(XiaoNrf54L15.cpuFrequencyHz());
}

static void runCyclePowerProfile()
{
  XiaoPowerProfile nextProfile = XIAO_POWER_PROFILE_PERFORMANCE;
  switch (XiaoNrf54L15.powerProfile()) {
    case XIAO_POWER_PROFILE_PERFORMANCE:
      nextProfile = XIAO_POWER_PROFILE_BALANCED;
      break;
    case XIAO_POWER_PROFILE_BALANCED:
      nextProfile = XIAO_POWER_PROFILE_ULTRA_LOW_POWER;
      break;
    case XIAO_POWER_PROFILE_ULTRA_LOW_POWER:
      nextProfile = XIAO_POWER_PROFILE_PERFORMANCE;
      break;
    default:
      nextProfile = XIAO_POWER_PROFILE_BALANCED;
      break;
  }

  const bool ok = XiaoNrf54L15.applyPowerProfile(nextProfile);
  Serial.print("power-profile: ");
  Serial.print(powerProfileName(nextProfile));
  Serial.print(" -> ");
  Serial.print(ok ? "ok" : "fail");
  Serial.print(" err=");
  Serial.println(XiaoNrf54L15.peripheralLastError());
  printStatus();
}

static void runWatchdogQuickTest()
{
  if (!g_watchdogStarted) {
    g_watchdogStarted = XiaoNrf54L15.watchdogStart(4000, false, true);
    Serial.println(g_watchdogStarted ? "watchdog: started (4s)" : "watchdog: start failed");
    if (!g_watchdogStarted) {
      Serial.print("watchdog: err=");
      Serial.println(XiaoNrf54L15.watchdogLastError());
      return;
    }
  }

  const bool fed = XiaoNrf54L15.watchdogFeed();
  Serial.print("watchdog: feed=");
  Serial.println(fed ? "ok" : "fail");
  if (!fed) {
    Serial.print("watchdog: err=");
    Serial.println(XiaoNrf54L15.watchdogLastError());
  }
}

static void runSleepTest()
{
  Serial.println("sleep: entering 1000 ms");
  digitalWrite(LED_BUILTIN, LOW);
  XiaoNrf54L15.sleepMs(1000);
  digitalWrite(LED_BUILTIN, HIGH);
  Serial.println("sleep: woke up");
}

#if MATRIX_HAS_BLE
static void ensureBle()
{
  if (g_bleInitialized) {
    return;
  }
  g_bleInitialized = BLE.begin("XIAO-MATRIX");
  if (!g_bleInitialized) {
    Serial.print("ble: begin failed err=");
    Serial.println(BLE.lastError());
  }
}

static void runBleScan()
{
  ensureBle();
  if (!g_bleInitialized) {
    return;
  }

  const bool ok = BLE.scan(2000);
  if (!ok) {
    Serial.print("ble: scan failed err=");
    Serial.println(BLE.lastError());
    return;
  }

  Serial.print("ble: hit addr=");
  Serial.print(BLE.address());
  Serial.print(" rssi=");
  Serial.print(BLE.rssi());
  Serial.print(" name=");
  Serial.println(BLE.name());
}

static void runBleConnect()
{
  ensureBle();
  if (!g_bleInitialized) {
    return;
  }

  if (!BLE.scan(2500)) {
    Serial.print("ble: pre-connect scan failed err=");
    Serial.println(BLE.lastError());
    return;
  }

  Serial.print("ble: connect target=");
  Serial.println(BLE.address());

  if (!BLE.connectLastScanResult(8000)) {
    Serial.print("ble: connect failed err=");
    Serial.println(BLE.lastError());
    return;
  }

  Serial.print("ble: connected=");
  Serial.println(BLE.connectedAddress());
  delay(1000);

  if (!BLE.disconnect()) {
    Serial.print("ble: disconnect failed err=");
    Serial.println(BLE.lastError());
    return;
  }
  Serial.println("ble: disconnected");
}
#endif

#if MATRIX_HAS_802154
static void onIeeeScanResult(uint16_t channel, uint16_t panId, uint16_t shortAddr, uint8_t lqi, bool associationPermitted)
{
  Serial.print("802154: result ch=");
  Serial.print(channel);
  Serial.print(" pan=0x");
  Serial.print(panId, HEX);
  Serial.print(" short=0x");
  Serial.print(shortAddr, HEX);
  Serial.print(" lqi=");
  Serial.print(lqi);
  Serial.print(" assoc=");
  Serial.println(associationPermitted ? "yes" : "no");
}

static void ensureIeee()
{
  if (g_ieeeInitialized) {
    return;
  }
  g_ieeeInitialized = IEEE802154.begin();
  if (!g_ieeeInitialized) {
    Serial.print("802154: begin failed err=");
    Serial.println(IEEE802154.lastError());
    return;
  }
  (void)IEEE802154.setChannel(15);
}

static void runIeeeScan()
{
  ensureIeee();
  if (!g_ieeeInitialized) {
    return;
  }

  const uint32_t maskChannel15 = (1UL << (15 - 1));
  if (!IEEE802154.passiveScan(maskChannel15, 80, onIeeeScanResult)) {
    Serial.print("802154: passiveScan failed err=");
    Serial.println(IEEE802154.lastError());
    return;
  }
  Serial.println("802154: passiveScan complete");
}
#endif

static void runCommand(char c)
{
  switch (c) {
    case 'h':
    case '?':
      printMenu();
      break;
    case 'p':
      printStatus();
      break;
    case 'a':
      runAdcWindow();
      break;
    case 'i':
      runI2cScan();
      break;
    case 's':
      runSpiLoopback();
      break;
    case 'u':
      runUart1Loopback();
      break;
    case 'f':
      runToggleCpu();
      break;
    case 'l':
      runCyclePowerProfile();
      break;
    case 'w':
      runWatchdogQuickTest();
      break;
    case 'z':
      runSleepTest();
      break;
    case 'b':
#if MATRIX_HAS_BLE
      runBleScan();
#else
      Serial.println("ble: unavailable in selected radio profile");
#endif
      break;
    case 'c':
#if MATRIX_HAS_BLE
      runBleConnect();
#else
      Serial.println("ble-central: unavailable in selected radio profile");
#endif
      break;
    case 'r':
#if MATRIX_HAS_802154
      runIeeeScan();
#else
      Serial.println("802154: unavailable in selected radio profile");
#endif
      break;
    default:
      Serial.print("unknown command: ");
      Serial.println(c);
      printMenu();
      break;
  }
}

void setup()
{
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);
  pinMode(PIN_BUTTON, INPUT_PULLUP);

  Serial.begin(115200);
  delay(300);

  Serial.println("hardware-validation-matrix: ready");
  printStatus();
  printMenu();
}

void loop()
{
  while (Serial.available() > 0) {
    const int ch = Serial.read();
    if (ch < 0) {
      break;
    }
    if (ch == '\r' || ch == '\n' || ch == ' ' || ch == '\t') {
      continue;
    }
    runCommand(static_cast<char>(ch));
  }

  if (millis() - g_lastMenuMs >= 15000U) {
    g_lastMenuMs = millis();
    Serial.println("matrix: send 'h' for menu");
  }

  XiaoNrf54L15.sleepMs(10);
}
