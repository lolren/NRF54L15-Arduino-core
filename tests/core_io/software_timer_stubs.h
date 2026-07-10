#ifndef NRF54_SOFTWARE_TIMER_TEST_STUBS_H
#define NRF54_SOFTWARE_TIMER_TEST_STUBS_H

#include <cstdint>

#define Arduino_h
#define CMSIS_H

extern uint32_t g_fake_millis;
extern uint32_t g_fake_primask;

inline uint32_t millis() { return g_fake_millis; }
inline uint32_t __get_PRIMASK() { return g_fake_primask; }
inline void __disable_irq() { g_fake_primask = 1U; }
inline void __set_PRIMASK(uint32_t state) { g_fake_primask = state; }

#endif
