#include "Arduino.h"
#include "variant.h"

#include <errno.h>
#include <generated/zephyr/autoconf.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/sys/util.h>

#define GPIO_SPEC(port_label, pin_num) \
    { \
        .port = DEVICE_DT_GET(DT_NODELABEL(port_label)), \
        .pin = (pin_num), \
        .dt_flags = 0, \
    }

namespace {

constexpr gpio_pin_t kRfSwitchPowerPin = 3U;
constexpr gpio_pin_t kRfSwitchSelectPin = 5U;
constexpr uint8_t kRfSwitchCeramic = 0U;
constexpr uint8_t kRfSwitchExternal = 1U;

uint8_t g_rf_switch_selection = kRfSwitchCeramic;

int applyRfSwitchSelection(uint8_t selection)
{
    const struct device *const gpio2 = DEVICE_DT_GET(DT_NODELABEL(gpio2));
    if (!device_is_ready(gpio2)) {
        return -ENODEV;
    }

    const uint8_t normalized_selection = (selection == kRfSwitchExternal) ? kRfSwitchExternal : kRfSwitchCeramic;

    int ret = gpio_pin_configure(gpio2, kRfSwitchPowerPin, GPIO_OUTPUT);
    if (ret < 0) {
        return ret;
    }

    ret = gpio_pin_set(gpio2, kRfSwitchPowerPin, 1);
    if (ret < 0) {
        return ret;
    }

    ret = gpio_pin_configure(gpio2, kRfSwitchSelectPin, GPIO_OUTPUT);
    if (ret < 0) {
        return ret;
    }

    ret = gpio_pin_set(gpio2, kRfSwitchSelectPin, normalized_selection == kRfSwitchExternal ? 1 : 0);
    if (ret < 0) {
        return ret;
    }

    g_rf_switch_selection = normalized_selection;
    return 0;
}

} // namespace

extern "C" void xiaoNrf54l15SetAntenna(xiao_nrf54l15_antenna_t antenna)
{
    (void)applyRfSwitchSelection(antenna == XIAO_NRF54L15_ANTENNA_EXTERNAL ? kRfSwitchExternal : kRfSwitchCeramic);
}

extern "C" xiao_nrf54l15_antenna_t xiaoNrf54l15GetAntenna(void)
{
    return g_rf_switch_selection == kRfSwitchExternal ? XIAO_NRF54L15_ANTENNA_EXTERNAL
                                                      : XIAO_NRF54L15_ANTENNA_CERAMIC;
}

extern "C" {

extern const struct gpio_dt_spec g_pin_map[] = {
    GPIO_SPEC(gpio1, 4),   // D0
    GPIO_SPEC(gpio1, 5),   // D1
    GPIO_SPEC(gpio1, 6),   // D2
    GPIO_SPEC(gpio1, 7),   // D3
    GPIO_SPEC(gpio1, 10),  // D4
    GPIO_SPEC(gpio1, 11),  // D5
    GPIO_SPEC(gpio2, 8),   // D6
    GPIO_SPEC(gpio2, 7),   // D7
    GPIO_SPEC(gpio2, 1),   // D8
    GPIO_SPEC(gpio2, 4),   // D9
    GPIO_SPEC(gpio2, 2),   // D10
    GPIO_SPEC(gpio0, 3),   // D11
    GPIO_SPEC(gpio0, 4),   // D12
    GPIO_SPEC(gpio2, 10),  // D13
    GPIO_SPEC(gpio2, 9),   // D14
    GPIO_SPEC(gpio2, 6),   // D15
    GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios),
    GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios),
};

extern const size_t g_pin_map_size = ARRAY_SIZE(g_pin_map);

#if DT_NODE_HAS_STATUS(DT_NODELABEL(adc), okay)
extern const struct adc_dt_spec g_adc_map[] = {
    ADC_DT_SPEC_STRUCT(DT_NODELABEL(adc), 0),
    ADC_DT_SPEC_STRUCT(DT_NODELABEL(adc), 1),
    ADC_DT_SPEC_STRUCT(DT_NODELABEL(adc), 2),
    ADC_DT_SPEC_STRUCT(DT_NODELABEL(adc), 3),
    ADC_DT_SPEC_STRUCT(DT_NODELABEL(adc), 4),
    ADC_DT_SPEC_STRUCT(DT_NODELABEL(adc), 5),
    ADC_DT_SPEC_STRUCT(DT_NODELABEL(adc), 6),
    ADC_DT_SPEC_STRUCT(DT_NODELABEL(adc), 7),
};
extern const size_t g_adc_map_size = ARRAY_SIZE(g_adc_map);
#else
extern const struct adc_dt_spec g_adc_map[] = {};
extern const size_t g_adc_map_size = 0;
#endif

#define ARDUINO_PWM_SPEC_EMPTY { .dev = NULL, .channel = 0, .period = 0, .flags = 0 }

#if DT_NODE_HAS_STATUS(DT_ALIAS(pwm0), okay)
#define ARDUINO_PWM0_SPEC PWM_DT_SPEC_GET(DT_ALIAS(pwm0))
#else
#define ARDUINO_PWM0_SPEC ARDUINO_PWM_SPEC_EMPTY
#endif

#if DT_NODE_HAS_STATUS(DT_ALIAS(pwm1), okay)
#define ARDUINO_PWM1_SPEC PWM_DT_SPEC_GET(DT_ALIAS(pwm1))
#else
#define ARDUINO_PWM1_SPEC ARDUINO_PWM_SPEC_EMPTY
#endif

#if DT_NODE_HAS_STATUS(DT_ALIAS(pwm2), okay)
#define ARDUINO_PWM2_SPEC PWM_DT_SPEC_GET(DT_ALIAS(pwm2))
#else
#define ARDUINO_PWM2_SPEC ARDUINO_PWM_SPEC_EMPTY
#endif

#if DT_NODE_HAS_STATUS(DT_ALIAS(pwm3), okay)
#define ARDUINO_PWM3_SPEC PWM_DT_SPEC_GET(DT_ALIAS(pwm3))
#else
#define ARDUINO_PWM3_SPEC ARDUINO_PWM_SPEC_EMPTY
#endif

extern const uint8_t g_pwm_pins[] = { PIN_D6, PIN_D7, PIN_D8, PIN_D9 };

extern const struct pwm_dt_spec g_pwm_map[] = {
    ARDUINO_PWM0_SPEC,
    ARDUINO_PWM1_SPEC,
    ARDUINO_PWM2_SPEC,
    ARDUINO_PWM3_SPEC,
};

extern const size_t g_pwm_map_size = ARRAY_SIZE(g_pwm_map);

void initVariant(void)
{
#if defined(ARDUINO_XIAO_NRF54L15_EXT_ANTENNA)
    xiaoNrf54l15SetAntenna(XIAO_NRF54L15_ANTENNA_EXTERNAL);
#else
    xiaoNrf54l15SetAntenna(XIAO_NRF54L15_ANTENNA_CERAMIC);
#endif
}

} // extern "C"
