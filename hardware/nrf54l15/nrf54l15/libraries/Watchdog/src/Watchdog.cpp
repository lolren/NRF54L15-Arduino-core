#include "Watchdog.h"

#include <XiaoNrf54L15.h>

bool WatchdogClass::begin(uint32_t timeoutMs, bool pauseInSleep, bool pauseInDebug)
{
    return XiaoNrf54L15.watchdogStart(timeoutMs, pauseInSleep, pauseInDebug);
}

bool WatchdogClass::feed()
{
    return XiaoNrf54L15.watchdogFeed();
}

bool WatchdogClass::stop()
{
    return XiaoNrf54L15.watchdogStop();
}

bool WatchdogClass::active() const
{
    return XiaoNrf54L15.watchdogActive();
}

int WatchdogClass::lastError() const
{
    return XiaoNrf54L15.watchdogLastError();
}

WatchdogClass Watchdog;
