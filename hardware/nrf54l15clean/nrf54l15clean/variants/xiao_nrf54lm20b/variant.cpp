/*
 * XIAO nRF54LM20A variant initialization.
 *
 * Sets up:
 * - RGB LED as output (all off initially)
 * - Button as input with pull-up
 * - System clocks already configured by SystemInit()
 */

#include "variant.h"
#include "Arduino.h"
#include "Wire.h"

namespace {

static constexpr uint8_t kPmicAddress = 0x6BU;
static constexpr uint8_t kPmicAdcBase = 0x05U;
static constexpr uint8_t kPmicIbatEnableOffset = 0x24U;
static constexpr uint32_t kPmicTwimBase = 0x500ED000UL;

static TwoWire g_pmicSleepWire(
    reinterpret_cast<NRF_TWIM_Type*>(kPmicTwimBase),
    PIN_PMIC_SDA,
    PIN_PMIC_SCL);

static void parkPmicPins() {
    NRF_P1->DIRCLR = (1UL << 17U) | (1UL << 18U);
    NRF_P1->PIN_CNF[17U] = GPIO_PIN_CNF_INPUT_Disconnect;
    NRF_P1->PIN_CNF[18U] = GPIO_PIN_CNF_INPUT_Disconnect;
}

}  // namespace

extern "C" int xiaoNrf54lm20PmicPrepareForSleep(void)
{
    g_pmicSleepWire.setClock(100000UL);
    g_pmicSleepWire.begin();
    g_pmicSleepWire.beginTransmission(kPmicAddress);
    const bool queued =
        g_pmicSleepWire.write(kPmicAdcBase) == 1U &&
        g_pmicSleepWire.write(kPmicIbatEnableOffset) == 1U &&
        g_pmicSleepWire.write(0U) == 1U;
    const uint8_t status = g_pmicSleepWire.endTransmission();
    g_pmicSleepWire.end();
    parkPmicPins();
    return (queued && status == 0U) ? 1 : 0;
}

extern "C" void initVariant(void)
{
    // Initialize RGB LED pins as outputs (off = HIGH for active-low)
    pinMode(PIN_LED_RED, OUTPUT);
    pinMode(PIN_LED_BLUE, OUTPUT);
    pinMode(PIN_LED_GREEN, OUTPUT);
    
    digitalWrite(PIN_LED_RED, HIGH);
    digitalWrite(PIN_LED_BLUE, HIGH);
    digitalWrite(PIN_LED_GREEN, HIGH);
    
    // Initialize button as input with pull-up
    pinMode(PIN_BUTTON, INPUT_PULLUP);

    // The onboard PY25Q64 flash is not used for code execution. Keep it in
    // deep power-down by default so simple low-power sketches are not charged
    // for an awake external flash. XiaoQspiFlash.begin() wakes it again.
    (void)xiaoNrf54lm20QspiFlashPrepareForSleep();
    
    // PMIC-controlled rails are intentionally opt-in. Sketches that need the
    // IMU/MIC rail should include npm1300.h and enable the required LDO.
}
