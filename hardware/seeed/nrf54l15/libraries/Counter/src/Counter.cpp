#include "Counter.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/counter.h>
#include <zephyr/kernel.h>

namespace {
const char *resolveCounterName()
{
#if DT_NODE_HAS_STATUS(DT_ALIAS(arduino_counter), okay)
    return DEVICE_DT_NAME(DT_ALIAS(arduino_counter));
#elif DT_NODE_HAS_STATUS(DT_NODELABEL(grtc), okay)
    return DEVICE_DT_NAME(DT_NODELABEL(grtc));
#else
    return nullptr;
#endif
}

const struct device *resolveCounter()
{
    const char *name = resolveCounterName();
    if (name == nullptr) {
        return nullptr;
    }
    return device_get_binding(name);
}
} // namespace

bool CounterClass::begin()
{
    const struct device *dev = resolveCounter();
    _counter = nullptr;
    _useSoftwareCounter = false;

    if (dev != nullptr && device_is_ready(dev)) {
        _counter = dev;
    } else {
        // Fallback so API still works on boards without a counter driver.
        _useSoftwareCounter = true;
    }

    _running = false;
    _softBaseMs = 0;
    _softStartMs = k_uptime_get();
    return (_counter != nullptr) || _useSoftwareCounter;
}

bool CounterClass::start()
{
    if (_counter == nullptr && !_useSoftwareCounter && !begin()) {
        return false;
    }

    if (_useSoftwareCounter) {
        _softStartMs = k_uptime_get();
        _running = true;
        return true;
    }

    const struct device *dev = static_cast<const struct device *>(_counter);
    if (dev == nullptr || counter_start(dev) != 0) {
        return false;
    }

    _running = true;
    return true;
}

bool CounterClass::stop()
{
    if (_useSoftwareCounter) {
        if (!_running) {
            return false;
        }
        _softBaseMs += (k_uptime_get() - _softStartMs);
        _running = false;
        return true;
    }

    const struct device *dev = static_cast<const struct device *>(_counter);
    if (dev == nullptr) {
        return false;
    }

    if (counter_stop(dev) != 0) {
        return false;
    }

    _running = false;
    return true;
}

bool CounterClass::reset()
{
    if (_useSoftwareCounter) {
        _softBaseMs = 0;
        _softStartMs = k_uptime_get();
        return true;
    }

    const struct device *dev = static_cast<const struct device *>(_counter);
    if (dev == nullptr) {
        return false;
    }

    return counter_reset(dev) == 0;
}

uint32_t CounterClass::read()
{
    if (_useSoftwareCounter) {
        uint64_t total = _softBaseMs;
        if (_running) {
            total += (k_uptime_get() - _softStartMs);
        }
        return (uint32_t)total;
    }

    const struct device *dev = static_cast<const struct device *>(_counter);
    if (dev == nullptr) {
        return 0;
    }

    uint32_t ticks = 0;
    if (counter_get_value(dev, &ticks) != 0) {
        return 0;
    }

    return ticks;
}

bool CounterClass::isRunning() const
{
    return _running;
}

CounterClass Counter;
