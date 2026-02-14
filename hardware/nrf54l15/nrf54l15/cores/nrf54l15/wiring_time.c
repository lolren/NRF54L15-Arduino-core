#include "Arduino.h"

#include <generated/zephyr/autoconf.h>

#include <zephyr/kernel.h>

unsigned long millis(void)
{
    return (unsigned long)k_uptime_get();
}

unsigned long micros(void)
{
    return (unsigned long)k_cyc_to_us_floor64(k_cycle_get_64());
}

void delay(unsigned long ms)
{
    if (ms == 0) {
        return;
    }

    k_sleep(K_MSEC(ms));
}

void delayMicroseconds(unsigned int us)
{
    if (us == 0) {
        return;
    }

    k_busy_wait(us);
}
