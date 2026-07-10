#include <cstddef>
#include <cstdint>

#include "cmsis.h"

#ifndef EXPECTED_LAST_IRQ
#error "EXPECTED_LAST_IRQ must name the highest external IRQ"
#endif

static_assert(offsetof(NVIC_Type, ISER) == 0x000U);
static_assert(offsetof(NVIC_Type, ICER) == 0x080U);
static_assert(offsetof(NVIC_Type, ISPR) == 0x100U);
static_assert(offsetof(NVIC_Type, ICPR) == 0x180U);
static_assert(offsetof(NVIC_Type, IABR) == 0x200U);
static_assert(offsetof(NVIC_Type, IP) == 0x300U);
static_assert(offsetof(NVIC_Type, STIR) == 0xE00U);
static_assert(sizeof(NVIC_Type) == 0xE04U);
static_assert(sizeof(((NVIC_Type*)nullptr)->IP) > EXPECTED_LAST_IRQ);

int main() {
  return 0;
}
