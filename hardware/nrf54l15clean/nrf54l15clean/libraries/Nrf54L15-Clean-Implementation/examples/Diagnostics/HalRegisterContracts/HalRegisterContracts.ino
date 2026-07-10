#include <Arduino.h>
#include <stddef.h>

#include <nrf54l15_hal.h>

using namespace nrf54l15;

constexpr size_t kTwisTxBase = offsetof(NRF_TWIS_Type, DMA) +
                               offsetof(NRF_TWIS_DMA_Type, TX);
static_assert(kTwisTxBase + offsetof(NRF_TWIS_DMA_TX_Type, PTR) == 0x73CU,
              "TWIS TX PTR offset changed");
static_assert(kTwisTxBase + offsetof(NRF_TWIS_DMA_TX_Type, MAXCNT) == 0x740U,
              "TWIS TX MAXCNT offset changed");
static_assert(kTwisTxBase + offsetof(NRF_TWIS_DMA_TX_Type, AMOUNT) == 0x744U,
              "TWIS TX AMOUNT offset changed");

static_assert(sizeof(NRF_PWM_SEQ_Type) == 0x20U,
              "PWM sequence configuration stride changed");
static_assert(pwm::SEQ_CONFIG_STRIDE == sizeof(NRF_PWM_SEQ_Type),
              "HAL PWM sequence stride does not match the device type");
static_assert(I2S_RXTXD_MAXCNT_MAXCNT_Msk == 0x3FFFUL,
              "I2S byte-count field changed");
static_assert(TIMER_INTENSET_COMPARE0_Pos == 16UL &&
                  TIMER_INTENSET_COMPARE1_Pos == 17UL,
              "TIMER compare interrupt bits must be consecutive from bit 16");
static_assert(offsetof(NRF_MEMCONF_Type, POWER) == 0x500U &&
                  offsetof(NRF_MEMCONF_POWER_Type, CONTROL) == 0U &&
                  offsetof(NRF_MEMCONF_POWER_Type, RET) == 8U,
              "MEMCONF RAM sections must map to bits in POWER[0]");
static_assert(offsetof(NRF_CRACENCORE_Type, PK) +
                      offsetof(NRF_CRACENCORE_PK_Type, COMMAND) ==
                  0x2004U &&
                  offsetof(NRF_CRACENCORE_Type, PK) +
                      offsetof(NRF_CRACENCORE_PK_Type, CONTROL) ==
                  0x2008U &&
                  offsetof(NRF_CRACENCORE_Type, PK) +
                      offsetof(NRF_CRACENCORE_PK_Type, STATUS) ==
                  0x200CU &&
                  offsetof(NRF_CRACENCORE_Type, IKG) +
                      offsetof(NRF_CRACENCORE_IKG_Type, PKECONTROL) ==
                  0x301CU,
              "CRACEN PKE register layout changed");
static_assert(xiao_nrf54l15::Memconf::kUnsupportedProtectionStatus ==
                  UINT32_MAX,
              "Unsupported MEMCONF protection state must not look enabled");
static_assert(!xiao_nrf54l15::Memconf::ramTransitionInterruptsSupported(),
              "MEMCONF must not advertise nonexistent transition IRQs");
static_assert(!xiao_nrf54l15::Oscillators::hfclkSourceSupported(
                  xiao_nrf54l15::Oscillators::HfclkSource::kSynt),
              "nRF54L has no LFCLK-synthesized HFCLK source");

#if defined(NRF54LM20A_XXAA) || defined(NRF54LM20B_XXAA)
static_assert(NRF54_HAS_I2S20 == 0, "LM20 must not expose I2S20");
static_assert(I2S20_BASE == 0U, "LM20 I2S HAL must fail closed");
static_assert(!xiao_nrf54l15::I2sTx::supported() &&
                  !xiao_nrf54l15::I2sRx::supported() &&
                  !xiao_nrf54l15::I2sDuplex::supported(),
              "LM20 I2S wrappers must reject every instance");
static_assert(GPIO_P3_BASE != 0U, "LM20 P3 must be routable");
static_assert(NRF_TAMPC_S_BASE == 0x500EF000UL,
              "LM20 TAMPC secure base changed");
static_assert(sizeof(NRF_CRACENCORE_RNGCONTROL_Type) == 0x100U,
              "LM20 CRACEN RNG layout changed");
static_assert(CRACENCORE_RNGCONTROL_STATUS_STARTUPFAIL_Pos == 10UL &&
                  CRACENCORE_RNGCONTROL_STATUS_REPTESTFAILPERSHARE_Pos ==
                      12UL &&
                  CRACENCORE_RNGCONTROL_STATUS_PROPTESTFAILPERSHARE_Pos ==
                      16UL &&
                  CRACENCORE_RNGCONTROL_STATUS_CONDITIONINGISTOOSLOW_Pos ==
                      20UL,
              "LM20 CRACEN RNG health bits changed");
static_assert(offsetof(NRF_CRACENCORE_Type, PK) == 0x2000U,
              "LM20 CRACEN PK offset changed");
static_assert(xiao_nrf54l15::CracenPke::pkeDataSize() == 15U * 512U,
              "LM20 PKE data RAM must expose 15 4096-bit pages");
static_assert(offsetof(NRF_MEMCONF_POWER_Type, RESERVED1) == 12U,
              "LM20 MEMCONF has no RET2 register");
#else
static_assert(NRF54_HAS_I2S20 == 1, "nRF54L15 must expose I2S20");
static_assert(I2S20_BASE != 0U, "nRF54L15 I2S base missing");
static_assert(xiao_nrf54l15::I2sTx::supported() &&
                  xiao_nrf54l15::I2sRx::supported() &&
                  xiao_nrf54l15::I2sDuplex::supported(),
              "nRF54L15 I2S wrappers unexpectedly disabled");
static_assert(GPIO_P3_BASE == 0U, "nRF54L15 must not expose P3");
static_assert(sizeof(NRF_CRACENCORE_RNGCONTROL_Type) == 0xC0U,
              "nRF54L15 CRACEN RNG layout changed");
static_assert(xiao_nrf54l15::CracenPke::pkeDataSize() == 8192U,
              "nRF54L15 PKE data RAM size changed");
static_assert(offsetof(NRF_MEMCONF_POWER_Type, RET2) == 12U,
              "nRF54L15 MEMCONF RET2 offset changed");
#endif

#if defined(NRF_TRUSTZONE_NONSECURE)
static_assert(NRF54_RRAMC_DIRECT_ACCESS_AVAILABLE == 0,
              "Non-secure RRAMC direct access must remain disabled");
static_assert(RRAMC_BASE == 0U, "Non-secure RRAMC must use a null sentinel");
#else
static_assert(NRF54_RRAMC_DIRECT_ACCESS_AVAILABLE == 1,
              "Secure RRAMC access unexpectedly disabled");
static_assert(RRAMC_BASE != 0U, "Secure RRAMC base missing");
#endif

void setup() {}
void loop() {}
