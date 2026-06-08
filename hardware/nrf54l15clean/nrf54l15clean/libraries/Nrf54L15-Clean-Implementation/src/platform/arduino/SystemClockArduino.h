/**
 * CHIP System Clock — Arduino Platform Adaptation
 *
 * Simplified clock implementation using Arduino's millis().
 * No <chrono> dependency to avoid macro conflicts.
 */

#pragma once

#include <Arduino.h>
#include <stdint.h>
#include <system/SystemError.h>

namespace chip {
namespace System {
namespace Clock {

// Simple duration types (no chrono dependency)
struct Microseconds64 {
    uint64_t count() const { return mCount; }
    Microseconds64(uint64_t c) : mCount(c) {}
    Microseconds64() : mCount(0) {}
    Microseconds64 operator+(const Microseconds64 & other) const { return Microseconds64(mCount + other.mCount); }
    Microseconds64 operator-(const Microseconds64 & other) const { return Microseconds64(mCount - other.mCount); }
    bool operator<(const Microseconds64 & other) const { return mCount < other.mCount; }
    bool operator>(const Microseconds64 & other) const { return mCount > other.mCount; }
    bool operator<=(const Microseconds64 & other) const { return mCount <= other.mCount; }
    bool operator>=(const Microseconds64 & other) const { return mCount >= other.mCount; }
private:
    uint64_t mCount;
};

struct Milliseconds64 {
    uint64_t count() const { return mCount; }
    Milliseconds64(uint64_t c) : mCount(c) {}
    Milliseconds64() : mCount(0) {}
    Milliseconds64 operator+(const Milliseconds64 & other) const { return Milliseconds64(mCount + other.mCount); }
    Milliseconds64 operator-(const Milliseconds64 & other) const { return Milliseconds64(mCount - other.mCount); }
    bool operator<(const Milliseconds64 & other) const { return mCount < other.mCount; }
    bool operator>(const Milliseconds64 & other) const { return mCount > other.mCount; }
    bool operator<=(const Milliseconds64 & other) const { return mCount <= other.mCount; }
    bool operator>=(const Milliseconds64 & other) const { return mCount >= other.mCount; }
private:
    uint64_t mCount;
};

struct Milliseconds32 {
    uint32_t count() const { return mCount; }
    Milliseconds32(uint32_t c) : mCount(c) {}
    Milliseconds32() : mCount(0) {}
    Milliseconds32 operator+(const Milliseconds32 & other) const { return Milliseconds32(mCount + other.mCount); }
    Milliseconds32 operator-(const Milliseconds32 & other) const { return Milliseconds32(mCount - other.mCount); }
    bool operator<(const Milliseconds32 & other) const { return mCount < other.mCount; }
    bool operator>(const Milliseconds32 & other) const { return mCount > other.mCount; }
    bool operator<=(const Milliseconds32 & other) const { return mCount <= other.mCount; }
    bool operator>=(const Milliseconds32 & other) const { return mCount >= other.mCount; }
private:
    uint32_t mCount;
};

struct Milliseconds16 {
    uint16_t count() const { return mCount; }
    Milliseconds16(uint16_t c) : mCount(c) {}
    Milliseconds16() : mCount(0) {}
private:
    uint16_t mCount;
};

struct Seconds64 {
    uint64_t count() const { return mCount; }
    Seconds64(uint64_t c) : mCount(c) {}
    Seconds64() : mCount(0) {}
private:
    uint64_t mCount;
};

struct Seconds32 {
    uint32_t count() const { return mCount; }
    Seconds32(uint32_t c) : mCount(c) {}
    Seconds32() : mCount(0) {}
private:
    uint32_t mCount;
};

struct Seconds16 {
    uint16_t count() const { return mCount; }
    Seconds16(uint16_t c) : mCount(c) {}
    Seconds16() : mCount(0) {}
private:
    uint16_t mCount;
};

inline constexpr Seconds16 kZero{ 0 };

// Type aliases
using Timestamp = Milliseconds64;
using Timeout = Milliseconds32;

// Clock implementation using Arduino millis()
class ClockBase {
public:
    virtual ~ClockBase() = default;
    virtual Microseconds64 GetMonotonicMicroseconds64() = 0;
    virtual Milliseconds64 GetMonotonicMilliseconds64() = 0;
    virtual CHIP_ERROR GetClock_RealTime(Microseconds64 & aCurTime) = 0;
    virtual CHIP_ERROR GetClock_RealTimeMS(Milliseconds64 & aCurTime) = 0;
    virtual CHIP_ERROR SetClock_RealTime(Microseconds64 aNewCurTime) = 0;
};

class ClockImpl : public ClockBase {
public:
    ~ClockImpl() override = default;
    Microseconds64 GetMonotonicMicroseconds64() override {
        return Microseconds64(millis() * 1000);
    }
    Milliseconds64 GetMonotonicMilliseconds64() override {
        return Milliseconds64(millis());
    }
    CHIP_ERROR GetClock_RealTime(Microseconds64 & aCurTime) override {
        return CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE;
    }
    CHIP_ERROR GetClock_RealTimeMS(Milliseconds64 & aCurTime) override {
        return CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE;
    }
    CHIP_ERROR SetClock_RealTime(Microseconds64 aNewCurTime) override {
        return CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE;
    }
};

// Global clock instance
static ClockImpl gClockImpl;

namespace Internal {
inline ClockBase * gClockBase = &gClockImpl;
}

inline ClockBase & SystemClock() {
    return *Internal::gClockBase;
}

} // namespace Clock
} // namespace System
} // namespace chip
