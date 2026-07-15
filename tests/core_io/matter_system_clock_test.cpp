#include <assert.h>
#include <stdint.h>

#include <system/SystemClock.h>

namespace {
uint64_t gMonotonicUs = 0U;
}

uint64_t nrf54l15_core_monotonic_time_us() { return gMonotonicUs; }

int main() {
  gMonotonicUs = 0x00000001FFFFFFFEULL;
  assert(chip::System::Clock::GetMicroseconds() == gMonotonicUs);
  assert(chip::System::Clock::GetMilliseconds() == gMonotonicUs / 1000ULL);

  gMonotonicUs = 0x0000010000000002ULL;
  assert(chip::System::Clock::GetMicroseconds() == gMonotonicUs);
  assert(chip::System::Clock::GetMilliseconds() == gMonotonicUs / 1000ULL);
  return 0;
}
