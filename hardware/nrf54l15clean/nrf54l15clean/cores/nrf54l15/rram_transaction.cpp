#include <cmsis.h>
#include <stdint.h>

namespace {
volatile uint8_t g_rramTransactionLocked = 0U;
}

extern "C" bool nrf54l15_rram_transaction_try_lock(void) {
  const uint32_t primask = __get_PRIMASK();
  __disable_irq();
  const bool acquired = g_rramTransactionLocked == 0U;
  if (acquired) {
    g_rramTransactionLocked = 1U;
  }
  __set_PRIMASK(primask);
  return acquired;
}

extern "C" void nrf54l15_rram_transaction_unlock(void) {
  const uint32_t primask = __get_PRIMASK();
  __disable_irq();
  g_rramTransactionLocked = 0U;
  __set_PRIMASK(primask);
}
