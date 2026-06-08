/**
 * CHIP System Platform Configuration for Arduino nRF54L15
 *
 * Platform-specific configuration for the CHIP System Layer.
 */

#ifndef SYSTEMPLATFORMCONFIG_H
#define SYSTEMPLATFORMCONFIG_H

// Use Arduino-specific implementation
#define CHIP_SYSTEM_LAYER_IMPL_CONFIG_FILE <platform/arduino/SystemLayerImplArduino.h>

// No locking (single-threaded Arduino loop)
#define CHIP_SYSTEM_CONFIG_NO_LOCKING 1

// No sockets (OpenThread provides networking)
#define CHIP_SYSTEM_CONFIG_USE_SOCKETS 0
#define CHIP_SYSTEM_CONFIG_USE_LWIP 0
#define CHIP_SYSTEM_CONFIG_USE_POSIX_TIME_FUNCTS 0

// Timer configuration
#define CHIP_SYSTEM_CONFIG_NUM_TIMERS 8

// Packet buffer configuration
#define CHIP_SYSTEM_CONFIG_PACKETBUFFER_POOL_SIZE 8
#define CHIP_SYSTEM_CONFIG_PACKETBUFFER_CAPACITY_MAX 1024

// No statistics
#define CHIP_CONFIG_LOG_MODULE_CHIP_SYSTEM_LAYER_DETAIL 0

// Use Arduino clock (no chrono)
#define CHIP_SYSTEM_CONFIG_USE_ARDUINO_CLOCK 1

#endif // SYSTEMPLATFORMCONFIG_H
