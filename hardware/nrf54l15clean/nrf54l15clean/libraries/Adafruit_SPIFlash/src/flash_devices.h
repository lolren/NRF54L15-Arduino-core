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

#define P25Q64HA                                                            \
  {                                                                         \
    (1UL << 23), 5000, 0x85, 0x60, 0x17, 104, 0x02, false, true, true,       \
    true, false, false, false                                               \
  }

#define P25Q64HA_ALT_JEDEC                                                  \
  {                                                                         \
    (1UL << 23), 5000, 0x85, 0x20, 0x17, 104, 0x02, false, true, true,       \
    true, false, false, false                                               \
  }

static const SPIFlash_Device_t possible_devices[] = {
    P25Q64HA,
    P25Q64HA_ALT_JEDEC,
};

static const size_t EXTERNAL_FLASH_DEVICE_COUNT =
    sizeof(possible_devices) / sizeof(possible_devices[0]);

#endif
