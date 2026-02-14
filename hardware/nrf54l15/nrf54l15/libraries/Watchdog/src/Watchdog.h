#ifndef Watchdog_h
#define Watchdog_h

#include <stdint.h>

class WatchdogClass {
public:
    bool begin(uint32_t timeoutMs, bool pauseInSleep = false, bool pauseInDebug = true);
    bool feed();
    bool stop();
    bool active() const;
    int lastError() const;
};

extern WatchdogClass Watchdog;

#endif
