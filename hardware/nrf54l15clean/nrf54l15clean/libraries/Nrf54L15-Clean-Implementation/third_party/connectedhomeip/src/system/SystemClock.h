#pragma once
#include <Arduino.h>
#include <Arduino.h>
namespace chip { namespace System {
struct Clock {
    static uint64_t GetMicroseconds() { return micros(); }
    static uint64_t GetMilliseconds() { return millis(); }
    using Timeout = uint32_t;
};
}}
