#include <cassert>
#include <cstdint>
#include <climits>

#define Arduino_h
#undef LONG_MIN
#undef LONG_MAX
#define LONG_MIN INT32_MIN
#define LONG_MAX INT32_MAX

extern "C" {
#include "wiring_math.c"
}

static long oracle(int32_t x, int32_t in_min, int32_t in_max,
                   int32_t out_min, int32_t out_max) {
  if (in_min == in_max) {
    return out_min;
  }
  const __int128 value =
      ((__int128)x - in_min) * ((__int128)out_max - out_min) /
          ((__int128)in_max - in_min) +
      out_min;
  if (value > INT32_MAX) return INT32_MAX;
  if (value < INT32_MIN) return INT32_MIN;
  return static_cast<long>(value);
}

int main() {
  const int32_t values[] = {INT32_MIN, INT32_MIN + 1, -123456789, -1,
                            0, 1, 123456789, INT32_MAX - 1, INT32_MAX};
  for (int32_t x : values) {
    assert(map(x, INT32_MIN, INT32_MAX, INT32_MIN, INT32_MAX) ==
           oracle(x, INT32_MIN, INT32_MAX, INT32_MIN, INT32_MAX));
    assert(map(x, INT32_MIN, INT32_MAX, INT32_MAX, INT32_MIN) ==
           oracle(x, INT32_MIN, INT32_MAX, INT32_MAX, INT32_MIN));
    assert(map(x, INT32_MAX, INT32_MIN, INT32_MIN, INT32_MAX) ==
           oracle(x, INT32_MAX, INT32_MIN, INT32_MIN, INT32_MAX));
  }
  assert(map(7, 3, 3, -9, 42) == -9);
  return 0;
}
