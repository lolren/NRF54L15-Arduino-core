#include "HPFMSPI.h"

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/mspi.h>

namespace {
#if DT_HAS_COMPAT_STATUS_OKAY(nordic_hpf_mspi_controller)
#define ARDUINO_HPF_MSPI_NODE DT_INST(0, nordic_hpf_mspi_controller)

uint32_t resolveHpfMaxFrequency()
{
    return DT_PROP(ARDUINO_HPF_MSPI_NODE, clock_frequency);
}
#else

uint32_t resolveHpfMaxFrequency()
{
    return 0U;
}
#endif
} // namespace

bool HPFMSPIClass::begin()
{
#if defined(CONFIG_MSPI) && DT_HAS_COMPAT_STATUS_OKAY(nordic_hpf_mspi_controller)
    const struct device *dev = DEVICE_DT_GET(ARDUINO_HPF_MSPI_NODE);
    if (dev == nullptr || !device_is_ready(dev)) {
        _initialized = false;
        _lastError = -ENODEV;
        return false;
    }

    _initialized = true;
    _lastError = 0;
    return true;
#elif defined(CONFIG_MSPI)
    _initialized = false;
    _lastError = -ENODEV;
    return false;
#else
    _initialized = false;
    _lastError = -ENOTSUP;
    return false;
#endif
}

bool HPFMSPIClass::available() const
{
    return _initialized;
}

bool HPFMSPIClass::isConfigured() const
{
#if defined(CONFIG_MSPI) && DT_HAS_COMPAT_STATUS_OKAY(nordic_hpf_mspi_controller)
    return true;
#else
    return false;
#endif
}

int HPFMSPIClass::channelStatus(uint8_t channel)
{
#if defined(CONFIG_MSPI) && DT_HAS_COMPAT_STATUS_OKAY(nordic_hpf_mspi_controller)
    if (!_initialized && !begin()) {
        return _lastError;
    }

    int status = mspi_get_channel_status(DEVICE_DT_GET(ARDUINO_HPF_MSPI_NODE), channel);
    _lastError = (status < 0) ? status : 0;
    return status;
#else
    ARG_UNUSED(channel);
    _lastError = isConfigured() ? -ENODEV : -ENOTSUP;
    return _lastError;
#endif
}

uint32_t HPFMSPIClass::maxFrequencyHz() const
{
    return resolveHpfMaxFrequency();
}

int HPFMSPIClass::lastError() const
{
    return _lastError;
}

String HPFMSPIClass::info() const
{
    String text;
    text += "configured=";
    text += isConfigured() ? "yes" : "no";
    text += " ready=";
    text += available() ? "yes" : "no";
    text += " maxHz=";
    text += String(maxFrequencyHz());
    text += " err=";
    text += String(_lastError);
    return text;
}

HPFMSPIClass HPFMSPI;
