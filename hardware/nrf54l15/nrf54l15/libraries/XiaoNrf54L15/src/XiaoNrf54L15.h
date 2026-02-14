#ifndef XIAO_NRF54L15_LIBRARY_H
#define XIAO_NRF54L15_LIBRARY_H

#include <Arduino.h>

enum XiaoAntennaMode {
    XIAO_ANTENNA_CERAMIC = 0,
    XIAO_ANTENNA_EXTERNAL = 1,
};

enum XiaoPeripheral {
    XIAO_PERIPHERAL_UART0 = 0,
    XIAO_PERIPHERAL_UART1 = 1,
    XIAO_PERIPHERAL_I2C0 = 2,
    XIAO_PERIPHERAL_SPI0 = 3,
    XIAO_PERIPHERAL_ADC = 4,
    XIAO_PERIPHERAL_PWM0 = 5,
};

enum XiaoPowerProfile {
    XIAO_POWER_PROFILE_PERFORMANCE = 0,
    XIAO_POWER_PROFILE_BALANCED = 1,
    XIAO_POWER_PROFILE_ULTRA_LOW_POWER = 2,
};

class XiaoNrf54L15Class {
public:
    void setAntenna(XiaoAntennaMode antenna);
    XiaoAntennaMode antenna() const;
    bool usingExternalAntenna() const;

    const char* radioProfileName() const;
    bool bleEnabled() const;
    bool ieee802154Enabled() const;

    void sleepMs(uint32_t ms) const;
    void sleepUs(uint32_t us) const;
    bool requestSystemOff() const;
    void systemOff() const;
    bool watchdogStart(uint32_t timeoutMs,
                       bool pauseInSleep = false,
                       bool pauseInDebug = true) const;
    bool watchdogFeed() const;
    bool watchdogStop() const;
    bool watchdogActive() const;
    int watchdogLastError() const;

    uint32_t resetCause() const;
    void clearResetCause() const;
    bool resetWasWatchdog() const;

    uint32_t cpuFrequencyHz() const;
    uint32_t cpuFrequencyFromToolsHz() const;
    bool setCpuFrequencyHz(uint32_t hz) const;
    bool setPeripheralEnabled(XiaoPeripheral peripheral, bool enabled) const;
    bool peripheralEnabled(XiaoPeripheral peripheral) const;
    int peripheralLastError() const;
    bool applyPowerProfile(XiaoPowerProfile profile) const;
    XiaoPowerProfile powerProfile() const;

    bool channelSoundingEnabled() const;
    bool ble6FeatureSetRequested() const;
    bool ble6FeatureSetEnabled() const;
};

extern XiaoNrf54L15Class XiaoNrf54L15;

#endif
