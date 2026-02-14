#include <zephyr/kernel.h>

extern "C" void __attribute__((weak)) init(void) {}
extern "C" void __attribute__((weak)) initVariant(void) {}
extern "C" void __attribute__((weak)) yield(void)
{
    k_yield();
}

extern "C" void setup(void) __attribute__((weak));
extern "C" void loop(void) __attribute__((weak));

int __attribute__((weak)) main(void)
{
    init();
    initVariant();
    if (setup != nullptr) {
        setup();
    }

    while (true) {
        if (loop != nullptr) {
            loop();
        }
        yield();
    }

    return 0;
}
