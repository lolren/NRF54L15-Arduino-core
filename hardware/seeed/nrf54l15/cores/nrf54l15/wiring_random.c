#include "Arduino.h"

#include <zephyr/random/random.h>

static uint32_t g_random_state;
static bool g_random_seeded;

static uint32_t nextRandomU32(void)
{
    if (!g_random_seeded) {
        uint32_t seed = sys_rand32_get();
        g_random_state = (seed == 0U) ? 0x12345678U : seed;
        g_random_seeded = true;
    }

    /* xorshift32 */
    uint32_t x = g_random_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;

    if (x == 0U) {
        x = 0x6b84211fU;
    }

    g_random_state = x;
    return x;
}

void randomSeed(unsigned long seed)
{
    if (seed == 0UL) {
        uint32_t hw = sys_rand32_get();
        g_random_state = (hw == 0U) ? 0x12345678U : hw;
    } else {
        g_random_state = (uint32_t)seed;
    }
    g_random_seeded = true;
}

long arduinoRandom(long max)
{
    if (max <= 0) {
        return 0;
    }

    return (long)(nextRandomU32() % (uint32_t)max);
}

long arduinoRandomRange(long min, long max)
{
    if (min >= max) {
        return min;
    }

    uint32_t span = (uint32_t)(max - min);
    return min + (long)(nextRandomU32() % span);
}
