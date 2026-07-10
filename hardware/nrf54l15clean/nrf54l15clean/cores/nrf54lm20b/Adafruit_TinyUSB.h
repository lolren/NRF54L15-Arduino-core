#ifndef ADAFRUIT_TINYUSB_ARDUINO_H_
#define ADAFRUIT_TINYUSB_ARDUINO_H_

// TinyUSB compatibility stub for nRF54L15 Clean Arduino Core.
// nRF54L15 uses the SAMD11 bridge for USB, not TinyUSB.
// This header provides compile-time compatibility only. No USB device stack
// is present, so status and transfer calls report the unsupported state.

#include "Arduino.h"

// USB PID/VID — XIAO nRF54L15 (2886:0066) / XIAO nRF54LM20A (2886:0068)
#define USB_VID 0x2886
#if defined(ARDUINO_NRF54LM20A) || defined(ARDUINO_NRF54LM20B)
#define USB_PID 0x0068
#else
#define USB_PID 0x0066
#endif

// TinyUSB constants for sketch compatibility
#define TUD_OPT_RHPORT   0
#define TUSB_OPT_DEVICE_ENABLED 0
#define NRF54_TINYUSB_DEVICE_SUPPORTED 0
#define BOARD_TUD_RHPORT 0
#define CFG_TUSB_RHPORT0_MODE OPT_MODE_DEVICE

// TinyUSB device stub
class TinyUSBDeviceClass {
public:
  bool begin(uint8_t rhport = 0) { (void)rhport; return false; }
  bool attach() { return false; }
  bool detach() { return false; }
  void setID(uint16_t vid, uint16_t pid) { (void)vid; (void)pid; }
  void setManufacturerDescriptor(const char* s) { (void)s; }
  void setProductDescriptor(const char* s) { (void)s; }
  void setSerialDescriptor(const char* s) { (void)s; }
  void setVersion(uint16_t v) { (void)v; }
  void setUSBVersion(uint16_t v) { setVersion(v); }
  void setDeviceVersion(uint16_t v) { (void)v; }
  bool ready() const { return false; }
  bool mounted() const { return false; }
  bool suspended() const { return false; }
  void setStringDescriptor(uint8_t idx, const char* s) { (void)idx; (void)s; }
};

extern TinyUSBDeviceClass TinyUSBDevice;

// tusb static inline stubs
static inline bool tud_cdc_connected(void) { return false; }
static inline bool tud_cdc_available(void) { return false; }
static inline int tud_cdc_read_char(void) { return -1; }
static inline uint32_t tud_cdc_write_char(char c) { (void)c; return 0U; }
static inline uint32_t tud_cdc_write_flush(void) { return 0U; }

// USB HID support (minimal stubs)
#include "class/hid/hid.h"

#endif /* ADAFRUIT_TINYUSB_ARDUINO_H_ */
