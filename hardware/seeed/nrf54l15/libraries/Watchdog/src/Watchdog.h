#ifndef Watchdog_h
#define Watchdog_h

#include <stdint.h>

class WatchdogClass {
public:
    bool begin(uint32_t timeoutMs);
    bool feed();

private:
    const void *_wdt = nullptr;
    int _channel = -1;
};

extern WatchdogClass Watchdog;

#endif
