#include "XiaoNrf54L15.h"

#include <errno.h>

#if defined(__has_include)
#if __has_include(<generated/zephyr/autoconf.h>)
#include <generated/zephyr/autoconf.h>
#endif
#if __has_include(<zephyr/device.h>)
#include <zephyr/device.h>
#endif
#if __has_include(<zephyr/devicetree.h>)
#include <zephyr/devicetree.h>
#endif
#if __has_include(<zephyr/drivers/hwinfo.h>)
#include <zephyr/drivers/hwinfo.h>
#endif
#if __has_include(<zephyr/drivers/watchdog.h>)
#include <zephyr/drivers/watchdog.h>
#endif
#if __has_include(<zephyr/kernel.h>)
#include <zephyr/kernel.h>
#endif
#if __has_include(<zephyr/pm/pm.h>)
#include <zephyr/pm/pm.h>
#endif
#if __has_include(<zephyr/pm/device.h>)
#include <zephyr/pm/device.h>
#endif
#if __has_include(<zephyr/pm/state.h>)
#include <zephyr/pm/state.h>
#endif
#if __has_include(<zephyr/sys/poweroff.h>)
#include <zephyr/sys/poweroff.h>
#endif
#else
#include <generated/zephyr/autoconf.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/kernel.h>
#include <zephyr/pm/pm.h>
#include <zephyr/pm/device.h>
#include <zephyr/pm/state.h>
#include <zephyr/sys/poweroff.h>
#endif

#include "nrf54l15.h"
#include "variant.h"

namespace {

bool g_watchdogRunning = false;
int g_watchdogChannel = -1;
int g_watchdogLastError = -ENODEV;
int g_peripheralLastError = 0;

constexpr uint32_t kCpuFreq64Hz = 64000000UL;
constexpr uint32_t kCpuFreq128Hz = 128000000UL;
constexpr uint32_t kCpuSwitchGuard = 1000000UL;

XiaoPowerProfile g_powerProfile = (static_cast<uint32_t>(F_CPU) >= kCpuFreq128Hz)
                                      ? XIAO_POWER_PROFILE_PERFORMANCE
                                      : XIAO_POWER_PROFILE_BALANCED;

const struct device* watchdogDevice()
{
#if DT_HAS_COMPAT_STATUS_OKAY(nordic_nrf_wdt)
    return DEVICE_DT_GET(DT_COMPAT_GET_ANY_STATUS_OKAY(nordic_nrf_wdt));
#else
    return nullptr;
#endif
}

uint32_t decodeCpuFrequencyHz(uint32_t currentField)
{
    if (currentField == OSCILLATORS_PLL_CURRENTFREQ_CURRENTFREQ_CK128M) {
        return kCpuFreq128Hz;
    }
    if (currentField == OSCILLATORS_PLL_CURRENTFREQ_CURRENTFREQ_CK64M) {
        return kCpuFreq64Hz;
    }
    return 0U;
}

uint32_t readCpuFrequencyHz()
{
    const uint32_t currentField =
        (NRF_OSCILLATORS->PLL.CURRENTFREQ & OSCILLATORS_PLL_CURRENTFREQ_CURRENTFREQ_Msk) >>
        OSCILLATORS_PLL_CURRENTFREQ_CURRENTFREQ_Pos;
    return decodeCpuFrequencyHz(currentField);
}

const struct device* peripheralDevice(XiaoPeripheral peripheral)
{
    switch (peripheral) {
    case XIAO_PERIPHERAL_UART0:
#if DT_NODE_HAS_STATUS(DT_CHOSEN(zephyr_console), okay)
        return DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
#elif DT_NODE_HAS_STATUS(DT_NODELABEL(uart20), okay)
        return DEVICE_DT_GET(DT_NODELABEL(uart20));
#else
        return nullptr;
#endif

    case XIAO_PERIPHERAL_UART1:
#if DT_NODE_HAS_STATUS(DT_NODELABEL(xiao_serial), okay)
        return DEVICE_DT_GET(DT_NODELABEL(xiao_serial));
#elif DT_NODE_HAS_STATUS(DT_NODELABEL(uart21), okay)
        return DEVICE_DT_GET(DT_NODELABEL(uart21));
#else
        return nullptr;
#endif

    case XIAO_PERIPHERAL_I2C0:
#if DT_NODE_HAS_STATUS(DT_NODELABEL(xiao_i2c), okay)
        return DEVICE_DT_GET(DT_NODELABEL(xiao_i2c));
#elif DT_NODE_HAS_STATUS(DT_NODELABEL(i2c22), okay)
        return DEVICE_DT_GET(DT_NODELABEL(i2c22));
#else
        return nullptr;
#endif

    case XIAO_PERIPHERAL_SPI0:
#if DT_NODE_HAS_STATUS(DT_NODELABEL(xiao_spi), okay)
        return DEVICE_DT_GET(DT_NODELABEL(xiao_spi));
#elif DT_NODE_HAS_STATUS(DT_NODELABEL(spi00), okay)
        return DEVICE_DT_GET(DT_NODELABEL(spi00));
#else
        return nullptr;
#endif

    case XIAO_PERIPHERAL_ADC:
#if DT_NODE_HAS_STATUS(DT_NODELABEL(adc), okay)
        return DEVICE_DT_GET(DT_NODELABEL(adc));
#else
        return nullptr;
#endif

    case XIAO_PERIPHERAL_PWM0:
#if DT_NODE_HAS_STATUS(DT_NODELABEL(pwm22), okay)
        return DEVICE_DT_GET(DT_NODELABEL(pwm22));
#else
        return nullptr;
#endif

    default:
        return nullptr;
    }
}

bool applyPeripheralIfSupported(const XiaoNrf54L15Class& self, XiaoPeripheral peripheral, bool enabled, int* outErr)
{
    if (self.setPeripheralEnabled(peripheral, enabled)) {
        return true;
    }

    const int err = self.peripheralLastError();
    if (err == -ENOTSUP || err == -ENOSYS || err == -ENODEV || err == -EALREADY) {
        return true;
    }

    if (outErr != nullptr) {
        *outErr = err;
    }
    return false;
}

} // namespace

void XiaoNrf54L15Class::setAntenna(XiaoAntennaMode antenna)
{
    xiaoNrf54l15SetAntenna(
        antenna == XIAO_ANTENNA_EXTERNAL
            ? XIAO_NRF54L15_ANTENNA_EXTERNAL
            : XIAO_NRF54L15_ANTENNA_CERAMIC);
}

XiaoAntennaMode XiaoNrf54L15Class::antenna() const
{
    return xiaoNrf54l15GetAntenna() == XIAO_NRF54L15_ANTENNA_EXTERNAL
               ? XIAO_ANTENNA_EXTERNAL
               : XIAO_ANTENNA_CERAMIC;
}

bool XiaoNrf54L15Class::usingExternalAntenna() const
{
    return antenna() == XIAO_ANTENNA_EXTERNAL;
}

const char* XiaoNrf54L15Class::radioProfileName() const
{
#if defined(ARDUINO_XIAO_NRF54L15_RADIO_DUAL)
    return "BLE + 802.15.4";
#elif defined(ARDUINO_XIAO_NRF54L15_RADIO_BLE_ONLY)
    return "BLE only";
#elif defined(ARDUINO_XIAO_NRF54L15_RADIO_802154_ONLY)
    return "802.15.4 only";
#else
    return "disabled";
#endif
}

bool XiaoNrf54L15Class::bleEnabled() const
{
#if defined(ARDUINO_XIAO_NRF54L15_RADIO_DUAL) || defined(ARDUINO_XIAO_NRF54L15_RADIO_BLE_ONLY)
    return true;
#else
    return false;
#endif
}

bool XiaoNrf54L15Class::ieee802154Enabled() const
{
#if defined(ARDUINO_XIAO_NRF54L15_RADIO_DUAL) || defined(ARDUINO_XIAO_NRF54L15_RADIO_802154_ONLY)
    return true;
#else
    return false;
#endif
}

void XiaoNrf54L15Class::sleepMs(uint32_t ms) const
{
    if (ms == 0U) {
        return;
    }
    k_sleep(K_MSEC(ms));
}

void XiaoNrf54L15Class::sleepUs(uint32_t us) const
{
    if (us == 0U) {
        return;
    }
    k_busy_wait(us);
}

bool XiaoNrf54L15Class::requestSystemOff() const
{
#if defined(CONFIG_PM)
    pm_state_info info{};
    info.state = PM_STATE_SOFT_OFF;
    info.substate_id = 0U;
    info.pm_device_disabled = false;
    info.min_residency_us = 0U;
    info.exit_latency_us = 0U;
    return pm_state_force(0U, &info);
#else
    return false;
#endif
}

void XiaoNrf54L15Class::systemOff() const
{
#if defined(CONFIG_POWEROFF)
    sys_poweroff();
#else
    for (;;) {
        k_sleep(K_FOREVER);
    }
#endif
}

bool XiaoNrf54L15Class::watchdogStart(uint32_t timeoutMs,
                                      bool pauseInSleep,
                                      bool pauseInDebug) const
{
    if (timeoutMs == 0U) {
        timeoutMs = 1U;
    }

    const struct device* dev = watchdogDevice();
    if (dev == nullptr || !device_is_ready(dev)) {
        g_watchdogLastError = -ENODEV;
        return false;
    }

    if (g_watchdogRunning && g_watchdogChannel >= 0) {
        g_watchdogLastError = 0;
        return true;
    }

    struct wdt_timeout_cfg cfg{};
    cfg.window.min = 0U;
    cfg.window.max = timeoutMs;
    cfg.callback = nullptr;
    cfg.flags = WDT_FLAG_RESET_SOC;

    const int channel = wdt_install_timeout(dev, &cfg);
    if (channel < 0) {
        g_watchdogLastError = channel;
        return false;
    }

    uint8_t options = 0U;
    if (pauseInSleep) {
        options |= WDT_OPT_PAUSE_IN_SLEEP;
    }
    if (pauseInDebug) {
        options |= WDT_OPT_PAUSE_HALTED_BY_DBG;
    }

    const int err = wdt_setup(dev, options);
    if (err < 0) {
        g_watchdogLastError = err;
        return false;
    }

    g_watchdogChannel = channel;
    g_watchdogRunning = true;
    g_watchdogLastError = 0;
    return true;
}

bool XiaoNrf54L15Class::watchdogFeed() const
{
    const struct device* dev = watchdogDevice();
    if (dev == nullptr || !device_is_ready(dev) || g_watchdogChannel < 0) {
        g_watchdogLastError = -ENODEV;
        return false;
    }

    const int err = wdt_feed(dev, g_watchdogChannel);
    g_watchdogLastError = err;
    if (err < 0) {
        return false;
    }

    g_watchdogRunning = true;
    return true;
}

bool XiaoNrf54L15Class::watchdogStop() const
{
    const struct device* dev = watchdogDevice();
    if (dev == nullptr || !device_is_ready(dev)) {
        g_watchdogLastError = -ENODEV;
        return false;
    }

    const int err = wdt_disable(dev);
    g_watchdogLastError = err;
    if (err < 0) {
        return false;
    }

    g_watchdogRunning = false;
    g_watchdogChannel = -1;
    return true;
}

bool XiaoNrf54L15Class::watchdogActive() const
{
    return g_watchdogRunning;
}

int XiaoNrf54L15Class::watchdogLastError() const
{
    return g_watchdogLastError;
}

uint32_t XiaoNrf54L15Class::resetCause() const
{
    uint32_t cause = 0U;
    if (hwinfo_get_reset_cause(&cause) < 0) {
        return 0U;
    }
    return cause;
}

void XiaoNrf54L15Class::clearResetCause() const
{
    (void)hwinfo_clear_reset_cause();
}

bool XiaoNrf54L15Class::resetWasWatchdog() const
{
    return (resetCause() & RESET_WATCHDOG) != 0U;
}

uint32_t XiaoNrf54L15Class::cpuFrequencyHz() const
{
    return readCpuFrequencyHz();
}

uint32_t XiaoNrf54L15Class::cpuFrequencyFromToolsHz() const
{
    return static_cast<uint32_t>(F_CPU);
}

bool XiaoNrf54L15Class::setCpuFrequencyHz(uint32_t hz) const
{
    uint32_t targetField = 0U;
    uint32_t expectedField = 0U;
    if (hz == kCpuFreq64Hz) {
        targetField = OSCILLATORS_PLL_FREQ_FREQ_CK64M;
        expectedField = OSCILLATORS_PLL_CURRENTFREQ_CURRENTFREQ_CK64M;
    } else if (hz == kCpuFreq128Hz) {
        targetField = OSCILLATORS_PLL_FREQ_FREQ_CK128M;
        expectedField = OSCILLATORS_PLL_CURRENTFREQ_CURRENTFREQ_CK128M;
    } else {
        return false;
    }

    NRF_OSCILLATORS->PLL.FREQ = (targetField << OSCILLATORS_PLL_FREQ_FREQ_Pos);

    uint32_t guard = 0U;
    while ((((NRF_OSCILLATORS->PLL.CURRENTFREQ & OSCILLATORS_PLL_CURRENTFREQ_CURRENTFREQ_Msk) >>
             OSCILLATORS_PLL_CURRENTFREQ_CURRENTFREQ_Pos) != expectedField) &&
           (guard++ < kCpuSwitchGuard)) {
        __NOP();
    }

    return readCpuFrequencyHz() == hz;
}

bool XiaoNrf54L15Class::setPeripheralEnabled(XiaoPeripheral peripheral, bool enabled) const
{
    const struct device* dev = peripheralDevice(peripheral);
    if (dev == nullptr || !device_is_ready(dev)) {
        g_peripheralLastError = -ENODEV;
        return false;
    }

#if defined(CONFIG_PM_DEVICE)
    const enum pm_device_action action = enabled ? PM_DEVICE_ACTION_RESUME : PM_DEVICE_ACTION_SUSPEND;
    int err = pm_device_action_run(dev, action);
    if (err == -EALREADY) {
        err = 0;
    }
    g_peripheralLastError = err;
    return err == 0;
#else
    (void)enabled;
    g_peripheralLastError = -ENOTSUP;
    return false;
#endif
}

bool XiaoNrf54L15Class::peripheralEnabled(XiaoPeripheral peripheral) const
{
    const struct device* dev = peripheralDevice(peripheral);
    if (dev == nullptr || !device_is_ready(dev)) {
        g_peripheralLastError = -ENODEV;
        return false;
    }

    enum pm_device_state state = PM_DEVICE_STATE_ACTIVE;
    const int err = pm_device_state_get(dev, &state);
    g_peripheralLastError = err;
    if (err < 0) {
        return false;
    }

    return state == PM_DEVICE_STATE_ACTIVE;
}

int XiaoNrf54L15Class::peripheralLastError() const
{
    return g_peripheralLastError;
}

bool XiaoNrf54L15Class::applyPowerProfile(XiaoPowerProfile profile) const
{
    int err = 0;

    switch (profile) {
    case XIAO_POWER_PROFILE_PERFORMANCE: {
        if (!setCpuFrequencyHz(kCpuFreq128Hz)) {
            g_peripheralLastError = -EIO;
            return false;
        }

        static const XiaoPeripheral kAllPeripherals[] = {
            XIAO_PERIPHERAL_UART0,
            XIAO_PERIPHERAL_UART1,
            XIAO_PERIPHERAL_I2C0,
            XIAO_PERIPHERAL_SPI0,
            XIAO_PERIPHERAL_ADC,
            XIAO_PERIPHERAL_PWM0,
        };

        for (size_t i = 0; i < (sizeof(kAllPeripherals) / sizeof(kAllPeripherals[0])); i++) {
            if (!applyPeripheralIfSupported(*this, kAllPeripherals[i], true, &err)) {
                g_peripheralLastError = err;
                return false;
            }
        }
        break;
    }

    case XIAO_POWER_PROFILE_BALANCED: {
        if (!setCpuFrequencyHz(kCpuFreq64Hz)) {
            g_peripheralLastError = -EIO;
            return false;
        }

        static const XiaoPeripheral kAllPeripherals[] = {
            XIAO_PERIPHERAL_UART0,
            XIAO_PERIPHERAL_UART1,
            XIAO_PERIPHERAL_I2C0,
            XIAO_PERIPHERAL_SPI0,
            XIAO_PERIPHERAL_ADC,
            XIAO_PERIPHERAL_PWM0,
        };

        for (size_t i = 0; i < (sizeof(kAllPeripherals) / sizeof(kAllPeripherals[0])); i++) {
            if (!applyPeripheralIfSupported(*this, kAllPeripherals[i], true, &err)) {
                g_peripheralLastError = err;
                return false;
            }
        }
        break;
    }

    case XIAO_POWER_PROFILE_ULTRA_LOW_POWER: {
        if (!setCpuFrequencyHz(kCpuFreq64Hz)) {
            g_peripheralLastError = -EIO;
            return false;
        }

        if (!applyPeripheralIfSupported(*this, XIAO_PERIPHERAL_UART0, true, &err) ||
            !applyPeripheralIfSupported(*this, XIAO_PERIPHERAL_UART1, false, &err) ||
            !applyPeripheralIfSupported(*this, XIAO_PERIPHERAL_I2C0, false, &err) ||
            !applyPeripheralIfSupported(*this, XIAO_PERIPHERAL_SPI0, false, &err) ||
            !applyPeripheralIfSupported(*this, XIAO_PERIPHERAL_ADC, false, &err) ||
            !applyPeripheralIfSupported(*this, XIAO_PERIPHERAL_PWM0, false, &err)) {
            g_peripheralLastError = err;
            return false;
        }
        break;
    }

    default:
        g_peripheralLastError = -EINVAL;
        return false;
    }

    g_powerProfile = profile;
    g_peripheralLastError = 0;
    return true;
}

XiaoPowerProfile XiaoNrf54L15Class::powerProfile() const
{
    return g_powerProfile;
}

bool XiaoNrf54L15Class::channelSoundingEnabled() const
{
#if defined(CONFIG_BT_CHANNEL_SOUNDING)
    return true;
#else
    return false;
#endif
}

bool XiaoNrf54L15Class::ble6FeatureSetRequested() const
{
#if defined(ARDUINO_XIAO_NRF54L15_BLE6_CS)
    return true;
#else
    return false;
#endif
}

bool XiaoNrf54L15Class::ble6FeatureSetEnabled() const
{
    return channelSoundingEnabled();
}

XiaoNrf54L15Class XiaoNrf54L15;
