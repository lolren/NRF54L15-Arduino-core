#include <assert.h>
#include <stdint.h>

#include "matter_message_counter.h"

using xiao_nrf54l15::MatterMessageCounter16;

int main() {
  MatterMessageCounter16 counter;
  assert(!counter.initialized());
  assert(counter.canAccept(0xFFFEU));
  assert(counter.accept(0xFFFEU));
  assert(counter.highest() == 0xFFFEU);

  assert(!counter.canAccept(0xFFFEU));
  assert(!counter.accept(0xFFFDU));
  assert(counter.accept(0xFFFFU));
  assert(counter.accept(0x0000U));
  assert(counter.accept(0x0001U));
  assert(!counter.accept(0xFFFFU));

  counter.reset();
  assert(!counter.initialized());
  assert(counter.accept(0U));
  assert(counter.canAccept(0x7FFFU));
  assert(!counter.canAccept(0x8000U));
  assert(counter.accept(0x7FFFU));
  assert(!counter.accept(0U));
  return 0;
}
