#ifndef ADAFRUIT_SPIFLASH_FLASH_DEVICES_H
#define ADAFRUIT_SPIFLASH_FLASH_DEVICES_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
  uint32_t total_size;
  uint32_t start_up_time_us;
  uint8_t manufacturer_id;
  uint8_t memory_type;
  uint8_t capacity;
  uint16_t max_clock_speed_mhz;
  uint8_t quad_enable_bit_mask;
  bool has_sector_protection;
  bool supports_fast_read;
  bool supports_qspi;
  bool supports_qspi_writes;
  bool write_status_register_split;
  bool single_status_byte;
  bool is_fram;
} SPIFlash_Device_t;

#define PY25Q64HA                                                           \
  {                                                                         \
    (1UL << 23), 5000, 0x85, 0x60, 0x17, 104, 0x02, false, true, true,       \
    true, false, false, false                                               \
  }

#define PY25Q64HA_ALT_JEDEC                                                 \
  {                                                                         \
    (1UL << 23), 5000, 0x85, 0x20, 0x17, 104, 0x02, false, true, true,       \
    true, false, false, false                                               \
  }

#define MX25R6435F                                                          \
  {                                                                         \
    (1UL << 23), 5000, 0xC2, 0x28, 0x17, 8, 0x40, false, true, true, true,   \
    false, true, false                                                      \
  }

// Backwards-compatible aliases for sketches written before the Puya part name
// was corrected to PY25Q64HA.
#define P25Q64HA PY25Q64HA
#define P25Q64HA_ALT_JEDEC PY25Q64HA_ALT_JEDEC

#endif
