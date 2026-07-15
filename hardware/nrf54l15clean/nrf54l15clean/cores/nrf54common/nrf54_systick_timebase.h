#ifndef NRF54_SYSTICK_TIMEBASE_H
#define NRF54_SYSTICK_TIMEBASE_H

#include <stdbool.h>
#include <stdint.h>

#ifndef NRF54_SYSTICK_MEMORY_BARRIER
#define NRF54_SYSTICK_MEMORY_BARRIER() __DMB()
#endif

typedef struct {
    volatile uint32_t generation;
    volatile uint32_t wrapCount[2];
} nrf54_systick_epoch_t;

#define NRF54_SYSTICK_EPOCH_INITIALIZER { 0U, { 0U, 0U } }

static inline void nrf54_systick_publish_tick(nrf54_systick_epoch_t* epoch)
{
    const uint32_t current = epoch->generation;
    const uint32_t next = current + 1U;
    uint32_t wraps = epoch->wrapCount[current & 1U];
    if (next == 0U) {
        ++wraps;
    }

    // Write the inactive slot first, then publish it with one atomic word.
    // A higher-priority interrupt therefore sees either complete epoch, even
    // when it preempts SysTick at the 32-bit millisecond rollover.
    epoch->wrapCount[next & 1U] = wraps;
    NRF54_SYSTICK_MEMORY_BARRIER();
    epoch->generation = next;
    NRF54_SYSTICK_MEMORY_BARRIER();
}

static inline uint64_t nrf54_systick_read_epoch(
    const nrf54_systick_epoch_t* epoch)
{
    uint32_t generationBefore;
    uint32_t generationAfter;
    uint32_t wraps;
    do {
        generationBefore = epoch->generation;
        NRF54_SYSTICK_MEMORY_BARRIER();
        wraps = epoch->wrapCount[generationBefore & 1U];
        NRF54_SYSTICK_MEMORY_BARRIER();
        generationAfter = epoch->generation;
    } while (generationBefore != generationAfter);

    return ((uint64_t)wraps << 32U) | (uint64_t)generationBefore;
}

static inline bool nrf54_systick_sample_is_stable(
    uint64_t epochBefore, uint64_t epochAfter, bool reloadObserved)
{
    return epochBefore == epochAfter && !reloadObserved;
}

static inline uint64_t nrf54_systick_compose_time_us(
    uint64_t completedMilliseconds, uint32_t currentValue,
    uint32_t reloadValue, uint32_t cyclesPerMicrosecond,
    bool tickPending)
{
    if (tickPending) {
        ++completedMilliseconds;
    }

    uint32_t elapsedCycles = 0U;
    if (currentValue <= reloadValue) {
        elapsedCycles = reloadValue - currentValue;
    }
    const uint32_t fractionalMicroseconds =
        cyclesPerMicrosecond == 0U ? 0U :
        (elapsedCycles / cyclesPerMicrosecond);

    return (completedMilliseconds * 1000ULL) +
           (uint64_t)fractionalMicroseconds;
}

#endif
