#include <assert.h>
#include <stdint.h>

#define NRF54_SYSTICK_MEMORY_BARRIER() do {} while (0)
#include "nrf54_systick_timebase.h"

int main() {
  nrf54_systick_epoch_t epoch = NRF54_SYSTICK_EPOCH_INITIALIZER;
  assert(nrf54_systick_read_epoch(&epoch) == 0ULL);

  nrf54_systick_publish_tick(&epoch);
  assert(nrf54_systick_read_epoch(&epoch) == 1ULL);

  // Writing the inactive wrap slot cannot expose a torn epoch to a reader.
  epoch.generation = UINT32_MAX;
  epoch.wrapCount[1] = 7U;
  epoch.wrapCount[0] = 8U;
  assert(nrf54_systick_read_epoch(&epoch) ==
         ((7ULL << 32U) | UINT32_MAX));
  epoch.generation = 0U;
  assert(nrf54_systick_read_epoch(&epoch) == (8ULL << 32U));

  epoch.generation = UINT32_MAX;
  epoch.wrapCount[1] = 11U;
  nrf54_systick_publish_tick(&epoch);
  assert(nrf54_systick_read_epoch(&epoch) == (12ULL << 32U));

  constexpr uint32_t kReload = 63999U;
  constexpr uint32_t kCyclesPerUs = 64U;
  const uint64_t beforeReload = nrf54_systick_compose_time_us(
      99U, 0U, kReload, kCyclesPerUs, false);
  const uint64_t pendingReload = nrf54_systick_compose_time_us(
      99U, kReload, kReload, kCyclesPerUs, true);
  const uint64_t servicedReload = nrf54_systick_compose_time_us(
      100U, kReload, kReload, kCyclesPerUs, false);
  assert(beforeReload == 99999ULL);
  assert(pendingReload == 100000ULL);
  assert(servicedReload == pendingReload);
  assert(pendingReload >= beforeReload);

  assert(nrf54_systick_sample_is_stable(100U, 100U, false));
  assert(!nrf54_systick_sample_is_stable(100U, 101U, false));
  assert(!nrf54_systick_sample_is_stable(100U, 100U, true));
  return 0;
}
