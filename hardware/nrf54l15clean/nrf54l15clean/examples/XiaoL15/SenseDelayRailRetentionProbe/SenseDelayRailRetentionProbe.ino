/*
  SenseDelayRailRetentionProbe

  Functional validation sketch for issue #43 on XIAO nRF54L15 Sense.

  Goal:
  - prove that plain delay() in `Low Power (WFI Idle)` does not silently
    collapse the XIAO board-control rails mid-sleep
  - sample IMU_MIC_EN / RF_SW / VBAT_EN halfway through delay() from a GRTC IRQ
  - immediately probe the Sense IMU and VBAT divider after wake

  Expected on a fixed core:
  - mid-delay IMU/VBAT samples stay HIGH while RF switch power is LOW
  - immediate post-delay WHO_AM_I reads keep succeeding on Sense boards
  - VBAT raw stays readable with VBAT_EN held HIGH

  Required board menu:
  - Power Profile: Low Power (WFI Idle)
  - BLE Support: Disabled (required)
*/

#include <Arduino.h>
#include <Wire.h>

#include "nrf54l15_hal.h"

#if !defined(NRF54L15_CLEAN_BLE_DISABLED) && \
    (!defined(NRF54L15_CLEAN_BLE_ENABLED) || \
     (NRF54L15_CLEAN_BLE_ENABLED != 0))
#error "SenseDelayRailRetentionProbe requires Tools -> BLE Support -> Disabled."
#endif

using namespace xiao_nrf54l15;

static Grtc g_grtc;

// BLE-disabled builds leave CC5 available while the core's tickless delay
// uses CC6. The compile-time guard above prevents an invalid BLE-on run.
static constexpr uint8_t kProbeCompareChannel = 5U;
static constexpr uint32_t kMidSampleDelayUs = 250000UL;
static constexpr uint8_t kImuWhoAmIReg = 0x0FU;
static const uint8_t kImuAddresses[] = {0x6AU, 0x6BU};

static volatile uint32_t g_midSampleCount = 0UL;
static volatile uint8_t g_midImuPin = 0U;
static volatile uint8_t g_midRfPin = 0U;
static volatile uint8_t g_midRfCtlPin = 0U;
static volatile uint8_t g_midVbatPin = 0U;
static volatile uint8_t g_midSampleArmed = 0U;

static bool g_haveImu = false;
static uint8_t g_imuAddress = 0U;
static uint8_t g_postImuPin = 0U;
static uint8_t g_postRfPin = 0U;
static uint8_t g_postRfCtlPin = 0U;
static uint8_t g_postVbatPin = 0U;
static uint8_t g_postImuOk = 0U;
static uint8_t g_postWhoAmI = 0U;
static int g_vbatRaw = -1;

static bool readWhoAmI(uint8_t address, uint8_t* whoAmI) {
  if (whoAmI == nullptr) {
    return false;
  }

  Wire1.beginTransmission(address);
  Wire1.write(kImuWhoAmIReg);
  if (Wire1.endTransmission(false) != 0U) {
    return false;
  }

  const int received = Wire1.requestFrom(static_cast<int>(address), 1, 1);
  if (received != 1 || Wire1.available() <= 0) {
    return false;
  }

  *whoAmI = static_cast<uint8_t>(Wire1.read());
  return true;
}

static bool detectSenseImu(uint8_t* address, uint8_t* whoAmI) {
  if (address == nullptr || whoAmI == nullptr) {
    return false;
  }

  for (size_t i = 0U; i < (sizeof(kImuAddresses) / sizeof(kImuAddresses[0])); ++i) {
    uint8_t id = 0U;
    if (readWhoAmI(kImuAddresses[i], &id)) {
      *address = kImuAddresses[i];
      *whoAmI = id;
      return true;
    }
  }

  return false;
}

static void armMidDelaySample() {
  g_midSampleArmed = 1U;
  (void)g_grtc.clearCompareEvent(kProbeCompareChannel);
  g_grtc.enableCompareInterrupt(kProbeCompareChannel, true);
  (void)g_grtc.setCompareAbsoluteUs(kProbeCompareChannel,
                                    g_grtc.counter() +
                                        static_cast<uint64_t>(kMidSampleDelayUs),
                                    true);
}

extern "C" void nrf54l15_grtc_irq_observer(void) {
  if (NRF_GRTC->EVENTS_COMPARE[kProbeCompareChannel] == 0U) {
    return;
  }

  NRF_GRTC->EVENTS_COMPARE[kProbeCompareChannel] = 0U;
  NRF_GRTC->CC[kProbeCompareChannel].CCEN =
      (GRTC_CC_CCEN_ACTIVE_Disable << GRTC_CC_CCEN_ACTIVE_Pos);
  NRF54L15_GRTC_INTENCLR_REG(NRF_GRTC) = (1UL << kProbeCompareChannel);

  g_midImuPin = static_cast<uint8_t>(digitalRead(IMU_MIC_EN) != 0 ? 1U : 0U);
  g_midRfPin = static_cast<uint8_t>(digitalRead(RF_SW) != 0 ? 1U : 0U);
  g_midRfCtlPin = static_cast<uint8_t>(digitalRead(RF_SW_CTL) != 0 ? 1U : 0U);
  g_midVbatPin = static_cast<uint8_t>(digitalRead(VBAT_EN) != 0 ? 1U : 0U);
  g_midSampleArmed = 0U;
  ++g_midSampleCount;
}

static void runRetentionMeasurement() {
  armMidDelaySample();
  delay(500);

  uint8_t whoAmI = 0U;
  g_postImuOk = static_cast<uint8_t>(
      g_haveImu && readWhoAmI(g_imuAddress, &whoAmI));
  g_postWhoAmI = whoAmI;
  g_postImuPin = static_cast<uint8_t>(digitalRead(IMU_MIC_EN) != 0 ? 1U : 0U);
  g_postRfPin = static_cast<uint8_t>(digitalRead(RF_SW) != 0 ? 1U : 0U);
  g_postRfCtlPin = static_cast<uint8_t>(digitalRead(RF_SW_CTL) != 0 ? 1U : 0U);
  g_postVbatPin = static_cast<uint8_t>(digitalRead(VBAT_EN) != 0 ? 1U : 0U);
  g_vbatRaw = analogRead(VBAT_READ);
}

static void reportMeasurement() {
  const bool passed =
      g_haveImu && g_midSampleCount == 1U && g_midSampleArmed == 0U &&
      g_midImuPin == 1U && g_midRfPin == 0U && g_midVbatPin == 1U &&
      g_postImuPin == 1U && g_postRfPin == 1U && g_postRfCtlPin == 0U &&
      g_postVbatPin == 1U && g_vbatRaw > 0 && g_postImuOk != 0U &&
      g_postWhoAmI == 0x6AU;

  Serial.print("mid_count=");
  Serial.print(g_midSampleCount);
  Serial.print(" armed=");
  Serial.print(g_midSampleArmed);
  Serial.print(" mid_imu=");
  Serial.print(g_midImuPin);
  Serial.print(" mid_rf=");
  Serial.print(g_midRfPin);
  Serial.print(" mid_rfctl=");
  Serial.print(g_midRfCtlPin);
  Serial.print(" mid_vbat=");
  Serial.print(g_midVbatPin);
  Serial.print(" post_imu=");
  Serial.print(g_postImuPin);
  Serial.print(" post_rf=");
  Serial.print(g_postRfPin);
  Serial.print(" post_rfctl=");
  Serial.print(g_postRfCtlPin);
  Serial.print(" post_vbat=");
  Serial.print(g_postVbatPin);
  Serial.print(" vbat_raw=");
  Serial.print(g_vbatRaw);
  Serial.print(" imu_ok=");
  Serial.print(g_postImuOk);
  if (g_postImuOk != 0U) {
    Serial.print(" who=0x");
    if (g_postWhoAmI < 0x10U) {
      Serial.print('0');
    }
    Serial.print(g_postWhoAmI, HEX);
  }
  Serial.print(" retention_status=");
  Serial.println(passed ? "PASS" : "FAIL");
}

void setup() {
  pinMode(IMU_MIC_EN, OUTPUT);
  pinMode(RF_SW, OUTPUT);
  pinMode(RF_SW_CTL, OUTPUT);
  pinMode(VBAT_EN, OUTPUT);

  digitalWrite(IMU_MIC_EN, HIGH);
  digitalWrite(RF_SW, HIGH);
  digitalWrite(RF_SW_CTL, LOW);
  digitalWrite(VBAT_EN, HIGH);

  Wire1.begin();
  Wire1.setClock(400000UL);

  // Do not configure bridge Serial before the measurement: plain delay()
  // intentionally skips board-state collapse while the bridge UART is active.
  // The settle window also initializes the core-owned low-power timebase.
  delay(10);

  uint8_t whoAmI = 0U;
  g_haveImu = detectSenseImu(&g_imuAddress, &whoAmI);
  runRetentionMeasurement();

  // Start the bridge only after the measured delay and report from retained RAM.
  Serial.begin(115200);
  Serial.println("SenseDelayRailRetentionProbe");
  Serial.println("Measured plain delay() before bridge Serial was configured.");
  Serial.print("sense_imu=");
  Serial.print(g_haveImu ? "yes" : "no");
  if (g_haveImu) {
    Serial.print(" addr=0x");
    if (g_imuAddress < 0x10U) {
      Serial.print('0');
    }
    Serial.print(g_imuAddress, HEX);
    Serial.print(" who=0x");
    if (whoAmI < 0x10U) {
      Serial.print('0');
    }
    Serial.println(whoAmI, HEX);
  } else {
    Serial.println(" (expected on non-Sense boards)");
  }
}

void loop() {
  reportMeasurement();
  delay(1000);
}
