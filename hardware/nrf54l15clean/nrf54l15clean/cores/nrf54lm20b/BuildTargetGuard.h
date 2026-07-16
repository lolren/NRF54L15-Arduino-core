#ifndef NRF54LM20_CLEAN_BUILD_TARGET_GUARD_H
#define NRF54LM20_CLEAN_BUILD_TARGET_GUARD_H

// Keep every object tied to the SoC core that compiled it. This turns a stale
// cross-board Arduino build cache into a linker error instead of a bad binary.
#if defined(__ASSEMBLER__)
    .weak __nrf54_core_target_reference_nrf54lm20
    .pushsection .gnu.linkonce.r.nrf54_core_target_guard.nrf54lm20,"a",%progbits
    .type __nrf54_core_target_reference_nrf54lm20,%object
    .size __nrf54_core_target_reference_nrf54lm20,4
__nrf54_core_target_reference_nrf54lm20:
    .word __nrf54_core_target_nrf54lm20
    .popsection
#else
#ifdef __cplusplus
extern "C" {
extern const unsigned char __nrf54_core_target_nrf54lm20;
inline const unsigned char* const __nrf54_core_target_reference_nrf54lm20
    __attribute__((used, section(
        ".gnu.linkonce.r.nrf54_core_target_guard.nrf54lm20"))) =
        &__nrf54_core_target_nrf54lm20;
}
#else
extern const unsigned char __nrf54_core_target_nrf54lm20;
const unsigned char* const __nrf54_core_target_reference_nrf54lm20
    __attribute__((weak, used, section(
        ".gnu.linkonce.r.nrf54_core_target_guard.nrf54lm20"))) =
        &__nrf54_core_target_nrf54lm20;
#endif
#endif

#endif  // NRF54LM20_CLEAN_BUILD_TARGET_GUARD_H
