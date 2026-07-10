#include <cassert>

#include "utility/SoftwareTimer.h"

uint32_t g_fake_millis = 0U;
uint32_t g_fake_primask = 0U;

static SoftwareTimer* g_self = nullptr;
static SoftwareTimer* g_victim = nullptr;
static unsigned g_self_calls = 0U;
static unsigned g_killer_calls = 0U;
static unsigned g_victim_calls = 0U;

static void deleteSelf(TimerHandle_t timer) {
  ++g_self_calls;
  assert(timer == g_self);
  delete g_self;
  g_self = nullptr;
}

static void deleteVictim(TimerHandle_t) {
  ++g_killer_calls;
  delete g_victim;
  g_victim = nullptr;
}

static void callVictim(TimerHandle_t) { ++g_victim_calls; }

int main() {
  g_fake_primask = 1U;
  g_self = new SoftwareTimer;
  assert(g_fake_primask == 1U);
  g_fake_primask = 0U;
  g_self->begin(1U, deleteSelf, nullptr, false);
  assert(g_self->start());
  g_fake_millis = 1U;
  SoftwareTimer::serviceAll();
  assert(g_self == nullptr);
  assert(g_self_calls == 1U);

  g_victim = new SoftwareTimer;
  g_victim->begin(1U, callVictim, nullptr, false);
  assert(g_victim->start());
  SoftwareTimer* killer = new SoftwareTimer;
  killer->begin(1U, deleteVictim, nullptr, false);
  assert(killer->start());
  g_fake_millis = 2U;
  SoftwareTimer::serviceAll();
  assert(g_victim == nullptr);
  assert(g_killer_calls == 1U);
  assert(g_victim_calls == 0U);
  delete killer;
  return 0;
}
