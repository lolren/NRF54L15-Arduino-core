#include "Watchdog.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/watchdog.h>

bool WatchdogClass::begin(uint32_t timeoutMs)
{
#if !DT_NODE_HAS_STATUS(DT_ALIAS(watchdog0), okay)
    return false;
#else
    const struct device *dev = DEVICE_DT_GET(DT_ALIAS(watchdog0));
    if (!device_is_ready(dev)) {
        return false;
    }

    struct wdt_timeout_cfg cfg = {
        .window = {
            .min = 0U,
            .max = timeoutMs,
        },
        .callback = NULL,
        .flags = WDT_FLAG_RESET_SOC,
    };

    _channel = wdt_install_timeout(dev, &cfg);
    if (_channel < 0) {
        return false;
    }

    if (wdt_setup(dev, 0) != 0) {
        _channel = -1;
        return false;
    }

    _wdt = dev;
    return true;
#endif
}

bool WatchdogClass::feed()
{
    const struct device *dev = static_cast<const struct device *>(_wdt);
    if (dev == nullptr || _channel < 0) {
        return false;
    }

    return wdt_feed(dev, _channel) == 0;
}

WatchdogClass Watchdog;
