#pragma once

#include <Arduino.h>
#include <stdint.h>

namespace chip {
namespace System {

struct Clock
{
    static uint64_t GetMicroseconds()
    {
        return nrf54l15_core_monotonic_time_us();
    }

    static uint64_t GetMilliseconds()
    {
        return nrf54l15_core_monotonic_time_us() / 1000ULL;
    }

    using Timeout = uint32_t;
};

} // namespace System
} // namespace chip
