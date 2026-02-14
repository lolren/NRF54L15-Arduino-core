#ifndef HPFMSPI_h
#define HPFMSPI_h

#include <Arduino.h>

#include <stdint.h>

class HPFMSPIClass {
public:
    bool begin();
    bool available() const;
    bool isConfigured() const;
    int channelStatus(uint8_t channel = 0U);
    uint32_t maxFrequencyHz() const;
    int lastError() const;
    String info() const;

private:
    bool _initialized = false;
    int _lastError = 0;
};

extern HPFMSPIClass HPFMSPI;

#endif
