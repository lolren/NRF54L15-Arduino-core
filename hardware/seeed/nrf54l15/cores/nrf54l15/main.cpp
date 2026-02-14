#include "Arduino.h"

#include <zephyr/kernel.h>

extern "C" void __attribute__((weak)) init(void) {}
extern "C" void __attribute__((weak)) initVariant(void) {}
extern "C" void __attribute__((weak)) yield(void)
{
    k_yield();
}

extern "C" void __attribute__((weak)) setup(void) {}
extern "C" void __attribute__((weak)) loop(void) {}

int main(void)
{
    init();
    initVariant();

    setup();

    for (;;) {
        loop();
        yield();
    }

    return 0;
}
