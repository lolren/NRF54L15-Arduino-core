#pragma once

// Keep feature selection identical in public headers and translation units.
// Explicitly disabled settings win over legacy aliases; an absent setting is
// unavailable so a header-only compile cannot turn into an undefined link.
#if defined(NRF54L15_CLEAN_ZIGBEE_DISABLED) ||                         \
    (defined(NRF54L15_CLEAN_ZIGBEE_ENABLED) &&                        \
     !(NRF54L15_CLEAN_ZIGBEE_ENABLED)) ||                             \
    (defined(NRF54L15_CLEAN_ZIGBEE_ENABLE) &&                         \
     !(NRF54L15_CLEAN_ZIGBEE_ENABLE))
#define NRF54L15_CLEAN_ZIGBEE_AVAILABLE 0
#elif (defined(NRF54L15_CLEAN_ZIGBEE_ENABLED) &&                      \
       NRF54L15_CLEAN_ZIGBEE_ENABLED) ||                              \
    (defined(NRF54L15_CLEAN_ZIGBEE_ENABLE) &&                         \
     NRF54L15_CLEAN_ZIGBEE_ENABLE)
#define NRF54L15_CLEAN_ZIGBEE_AVAILABLE 1
#else
#define NRF54L15_CLEAN_ZIGBEE_AVAILABLE 0
#endif

#if NRF54L15_CLEAN_ZIGBEE_AVAILABLE &&                               \
    defined(NRF54L15_CLEAN_BLE_ENABLED) &&                            \
    NRF54L15_CLEAN_BLE_ENABLED
#error "Experimental Zigbee currently requires Tools > Bluetooth LE > Disabled; shared RADIO arbitration is not complete"
#endif

#if NRF54L15_CLEAN_ZIGBEE_AVAILABLE &&                               \
    defined(NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE) &&                 \
    NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE
#error "Experimental Zigbee and OpenThread cannot share RADIO yet; enable only one 802.15.4 stack"
#endif
