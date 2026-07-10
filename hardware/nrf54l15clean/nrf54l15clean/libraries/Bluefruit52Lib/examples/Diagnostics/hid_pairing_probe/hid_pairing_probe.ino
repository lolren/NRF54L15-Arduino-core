/*
 * HID pairing probe
 *
 * A minimal HOGP peripheral based on blehid_mouse. It deliberately uses
 * NoInputNoOutput Secure Connections and emits only state transitions so it
 * can diagnose phone pairing without adding serial-load noise to the link.
 */

#include <bluefruit.h>
#if defined(NRF54L15_CLEAN_BLE_TRACE) && (NRF54L15_CLEAN_BLE_TRACE == 1)
#include <nrf54l15_hal.h>
#endif

BLEDis bledis;
BLEBas blebas;
BLEHidAdafruit blehid;

static bool connected = false;
static bool encrypted = false;
static uint32_t lastMouseReportMs = 0U;
static int8_t mouseDelta = 2;
#ifndef HID_PAIRING_PROBE_CLEAR_BONDS_ON_START
#define HID_PAIRING_PROBE_CLEAR_BONDS_ON_START 0
#endif
#ifndef HID_PAIRING_PROBE_USE_FIXED_ADDRESS
#define HID_PAIRING_PROBE_USE_FIXED_ADDRESS 0
#endif
#ifndef HID_PAIRING_PROBE_ZEPHYR_MOUSE_PROFILE
#define HID_PAIRING_PROBE_ZEPHYR_MOUSE_PROFILE 1
#endif

#if HID_PAIRING_PROBE_USE_FIXED_ADDRESS
static const ble_gap_addr_t kFixedProbeAddress = {
    {0x54U, 0xA1U, 0x17U, 0x54U, 0x58U, 0xC3U},
    BLE_GAP_ADDR_TYPE_RANDOM_STATIC,
};
#endif
#if defined(NRF54L15_CLEAN_BLE_TRACE) && (NRF54L15_CLEAN_BLE_TRACE == 1)
static constexpr uint8_t kTraceCapacity = 96U;
static const char* traceMessages[kTraceCapacity]{};
static uint8_t traceWriteIndex = 0U;
static uint8_t traceCount = 0U;
static bool traceCaptureEnabled = false;
static bool traceDumpPending = false;
static bool radioDebugDumpPending = false;
#endif

static void startAdv();
static void connectCallback(uint16_t connHandle);
static void disconnectCallback(uint16_t connHandle, uint8_t reason);
static void securedCallback(uint16_t connHandle);
static void pairCompleteCallback(uint16_t connHandle, uint8_t authStatus);
#if defined(NRF54L15_CLEAN_BLE_TRACE) && (NRF54L15_CLEAN_BLE_TRACE == 1)
static void traceCallback(const char* message, void* context);
static void clearTrace();
static void dumpTrace();
#endif

void setup() {
  Serial.begin(115200);
  for (uint32_t start = millis(); !Serial && (millis() - start) < 1500U;) {
    delay(10);
  }

  Serial.println("HID pairing probe ready");
  Bluefruit.begin();
#if defined(NRF54L15_CLEAN_BLE_TRACE) && (NRF54L15_CLEAN_BLE_TRACE == 1)
  Bluefruit.rawRadio().setTraceCallback(traceCallback, nullptr);
#endif
#if HID_PAIRING_PROBE_USE_FIXED_ADDRESS
  if (!Bluefruit.setAddr(&kFixedProbeAddress)) {
    Serial.println("fixed address setup failed");
  }
#endif
#if HID_PAIRING_PROBE_CLEAR_BONDS_ON_START
  Bluefruit.Periph.clearBonds();
#endif
  Bluefruit.setName("X54-HID-Probe");
  Bluefruit.setAppearance(BLE_APPEARANCE_HID_MOUSE);
  Bluefruit.setTxPower(4);
  Bluefruit.Security.setIOCaps(false, false, false);
  Bluefruit.Security.setSecuredCallback(securedCallback);
  Bluefruit.Security.setPairCompleteCallback(pairCompleteCallback);
  Bluefruit.Periph.setConnInterval(24, 40);
  Bluefruit.Periph.setConnectCallback(connectCallback);
  Bluefruit.Periph.setDisconnectCallback(disconnectCallback);

  bledis.setManufacturer("Adafruit Industries");
  bledis.setModel("Bluefruit Feather 52");
  bledis.begin();
  blebas.begin();
  blebas.write(100);
  blehid.setZephyrCompatibleMouse(HID_PAIRING_PROBE_ZEPHYR_MOUSE_PROFILE != 0);
  blehid.begin();
  // BLEHidAdafruit selects a faster default for the full profile on L15.
  // Keep the probe's interval fixed so the profile variants are comparable.
  Bluefruit.Periph.setConnInterval(24, 40);

  startAdv();
}

void loop() {
  const bool nowConnected = Bluefruit.connected();
  const bool nowEncrypted = nowConnected && Bluefruit.Security.isEncrypted(0U);
  if (nowConnected != connected) {
    connected = nowConnected;
    Serial.println(connected ? "connected" : "disconnected");
  }
  if (nowEncrypted != encrypted) {
    encrypted = nowEncrypted;
    Serial.println(encrypted ? "encrypted" : "unencrypted");
  }
  if (nowEncrypted && blehid.mouseNotifyEnabled() &&
      (millis() - lastMouseReportMs) >= 3000U) {
    lastMouseReportMs = millis();
    const bool sent = blehid.mouseMove(mouseDelta, 0);
    mouseDelta = static_cast<int8_t>(-mouseDelta);
    Serial.println(sent ? "mouse report sent" : "mouse report blocked");
  }
#if defined(NRF54L15_CLEAN_BLE_TRACE) && (NRF54L15_CLEAN_BLE_TRACE == 1)
  if (traceDumpPending && !nowConnected) {
    traceDumpPending = false;
    dumpTrace();
  }
  if (radioDebugDumpPending && !nowConnected) {
    radioDebugDumpPending = false;
    Bluefruit.debugPrintEncryptionCounters(Serial);
    Bluefruit.debugPrintDisconnectDebug(Serial);
    Bluefruit.debugPrintSecureConnectionsState(Serial);
  }
#endif
}

static void startAdv() {
  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addTxPower();
  Bluefruit.Advertising.addAppearance(BLE_APPEARANCE_HID_MOUSE);
  Bluefruit.Advertising.addService(blehid, blebas, bledis);
  Bluefruit.ScanResponse.addName();
  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.setInterval(32, 244);
  Bluefruit.Advertising.setFastTimeout(30);
  Bluefruit.Advertising.start(0);
}

static void connectCallback(uint16_t connHandle) {
  (void)connHandle;
#if defined(NRF54L15_CLEAN_BLE_TRACE) && (NRF54L15_CLEAN_BLE_TRACE == 1)
  clearTrace();
  traceCaptureEnabled = true;
#endif
  Serial.println("connect callback");
  Bluefruit.Security.requestPairing();
}

static void disconnectCallback(uint16_t connHandle, uint8_t reason) {
  (void)connHandle;
#if defined(NRF54L15_CLEAN_BLE_TRACE) && (NRF54L15_CLEAN_BLE_TRACE == 1)
  traceCaptureEnabled = false;
  traceDumpPending = true;
  radioDebugDumpPending = true;
#endif
  Serial.print("disconnect reason=0x");
  Serial.println(reason, HEX);
}

static void securedCallback(uint16_t connHandle) {
  (void)connHandle;
  Serial.println("secured callback");
}

static void pairCompleteCallback(uint16_t connHandle, uint8_t authStatus) {
  (void)connHandle;
  Serial.print("pair status=0x");
  Serial.println(authStatus, HEX);
}

#if defined(NRF54L15_CLEAN_BLE_TRACE) && (NRF54L15_CLEAN_BLE_TRACE == 1)
static void traceCallback(const char* message, void* context) {
  (void)context;
  if (!traceCaptureEnabled || message == nullptr) {
    return;
  }
  traceMessages[traceWriteIndex] = message;
  traceWriteIndex = static_cast<uint8_t>((traceWriteIndex + 1U) % kTraceCapacity);
  if (traceCount < kTraceCapacity) {
    ++traceCount;
  }
}

static void clearTrace() {
  traceWriteIndex = 0U;
  traceCount = 0U;
}

static void dumpTrace() {
  Serial.println("trace begin");
  uint8_t index = static_cast<uint8_t>((traceWriteIndex + kTraceCapacity - traceCount) %
                                       kTraceCapacity);
  for (uint8_t remaining = traceCount; remaining > 0U; --remaining) {
    if (traceMessages[index] != nullptr) {
      Serial.println(traceMessages[index]);
    }
    index = static_cast<uint8_t>((index + 1U) % kTraceCapacity);
  }
  Serial.println("trace end");
}
#endif
