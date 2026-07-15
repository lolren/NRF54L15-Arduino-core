#define OPENTHREAD_CONFIG_CRYPTO_LIB_MBEDTLS 1
#define OPENTHREAD_CONFIG_CRYPTO_LIB_PLATFORM 2
#include "openthread-core-user-config.h"

static_assert(OPENTHREAD_CONFIG_CRYPTO_LIB ==
                  OPENTHREAD_CONFIG_CRYPTO_LIB_MBEDTLS,
              "the staged core uses mbedTLS and needs independent PAL entropy");
static_assert(OPENTHREAD_CONFIG_MAC_SOFTWARE_RX_TIMING_ENABLE == 1,
              "the PAL relies on OpenThread software RX timing");
static_assert(OPENTHREAD_CONFIG_MAC_SOFTWARE_CSMA_BACKOFF_ENABLE == 1,
              "the PAL relies on OpenThread software CSMA backoff");
static_assert(OPENTHREAD_CONFIG_MAC_SOFTWARE_RX_ON_WHEN_IDLE_ENABLE == 1,
              "the PAL relies on OpenThread RX-on-when-idle handling");
static_assert(OPENTHREAD_CONFIG_CHILD_SUPERVISION_INTERVAL == 129,
              "child supervision must remain enabled");
static_assert(OPENTHREAD_CONFIG_CHILD_SUPERVISION_CHECK_TIMEOUT == 190,
              "child supervision timeout must remain enabled");
static_assert(
    OPENTHREAD_CONFIG_CHILD_SUPERVISION_OLDER_VERSION_CHILD_DEFAULT_INTERVAL ==
        129,
    "older children need a supervision interval");
static_assert(OPENTHREAD_CONFIG_DATASET_UPDATER_ENABLE == 1,
              "dataset updater sources are compiled into the staged core");
static_assert(OPENTHREAD_CONFIG_PARENT_SEARCH_ENABLE == 1,
              "detached and degraded children need parent search");

int main() { return 0; }
