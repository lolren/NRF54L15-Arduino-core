/*
 * Arduino Math Functions
 *
 * map() function
 *
 * Licensed under the Apache License 2.0
 */

#include "Arduino.h"
#include <limits.h>

static uint64_t magnitude_i64(int64_t value)
{
    return (value < 0) ? (uint64_t)(-(value + 1)) + 1U : (uint64_t)value;
}

long map(long x, long in_min, long in_max, long out_min, long out_max)
{
    // Handle edge case where input range is zero
    if (in_min == in_max) {
        return out_min;
    }

    const int64_t input_offset = (int64_t)x - (int64_t)in_min;
    const int64_t input_range = (int64_t)in_max - (int64_t)in_min;
    const int64_t output_range = (int64_t)out_max - (int64_t)out_min;
    const uint64_t scaled_magnitude =
        (magnitude_i64(input_offset) * magnitude_i64(output_range)) /
        magnitude_i64(input_range);
    const bool negative =
        ((input_offset < 0) != (output_range < 0)) != (input_range < 0);
    if (scaled_magnitude > (uint64_t)INT64_MAX) {
        return negative ? LONG_MIN : LONG_MAX;
    }
    int64_t result = negative ? -(int64_t)scaled_magnitude : (int64_t)scaled_magnitude;
    if ((out_min > 0 && result > INT64_MAX - (int64_t)out_min) ||
        (out_min < 0 && result < INT64_MIN - (int64_t)out_min)) {
        return (out_min > 0) ? LONG_MAX : LONG_MIN;
    }
    result += (int64_t)out_min;
    if (result > (int64_t)LONG_MAX) {
        return LONG_MAX;
    }
    if (result < (int64_t)LONG_MIN) {
        return LONG_MIN;
    }
    return (long)result;
}
