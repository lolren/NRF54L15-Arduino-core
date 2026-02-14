#ifndef SOC_H_
#define SOC_H_

/*
 * Minimal fallback for Zephyr header discovery before zephyr_lib is generated.
 * The real SoC header from Zephyr is copied into zephyr_lib/include/soc.h.
 */

#include "cmsis.h"

#ifndef NUM_IRQ_PRIO_BITS
#define NUM_IRQ_PRIO_BITS __NVIC_PRIO_BITS
#endif

#endif /* SOC_H_ */
