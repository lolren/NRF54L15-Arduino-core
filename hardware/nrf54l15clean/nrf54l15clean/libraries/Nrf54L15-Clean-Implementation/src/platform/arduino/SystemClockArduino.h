#pragma once

// Keep the Arduino-facing include path as an alias of the canonical staged
// CHIP clock. Defining a second clock API here makes the two headers mutually
// incompatible when a sketch includes both platform surfaces.
#include <system/SystemClock.h>
