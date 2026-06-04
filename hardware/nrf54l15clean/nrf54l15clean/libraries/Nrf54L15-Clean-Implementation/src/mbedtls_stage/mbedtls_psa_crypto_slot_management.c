#if defined(NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE) && \
    (NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE != 0) && \
    defined(NRF54L15_CLEAN_OPENTHREAD_MESHCOP_ENABLE) && \
    (NRF54L15_CLEAN_OPENTHREAD_MESHCOP_ENABLE != 0)
#include "../../third_party/openthread-core/third_party/mbedtls/repo/library/psa_crypto_slot_management.c"
#endif
