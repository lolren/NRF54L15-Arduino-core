/*
  GATT edge-case diagnostic

  This exposes one custom service for checking ATT/GATT behavior from a phone,
  desktop scanner, or another central:

  Service 0xFEE0
  - 0xFEE1: 244-byte readable/notifiable value. Read it with MTU 23 and MTU 247
            to exercise Read Blob offsets.
  - 0xFEE2: 244-byte readable/writable value. Write a long value to exercise
            queued Prepare Write / Execute Write, then read it back.
  - 0xFEE3: short descriptor probe with readable 0x2901, 0x2904, and 0x2908
            descriptors. The 0x2901 User Description is writable.

  Suggested manual test:
  1. Connect with nRF Connect or another BLE scanner.
  2. Request MTU 247, then read 0xFEE1. It should return 244 bytes.
  3. Write 244 bytes to 0xFEE2, then read 0xFEE2 back and compare.
  4. Try a 245-byte queued write to 0xFEE2. It must fail with Invalid
     Attribute Value Length and leave the previous value unchanged.
  5. Try a two-byte write to fixed-length 0xFEE3. It must fail with Invalid
     Attribute Value Length and leave the one-byte value unchanged.
  6. Write a new label to the 0x2901 descriptor under 0xFEE3, then read it.
*/

#include <bluefruit.h>

static constexpr uint16_t kServiceUuid = 0xFEE0;
static constexpr uint16_t kLongReadUuid = 0xFEE1;
static constexpr uint16_t kLongWriteUuid = 0xFEE2;
static constexpr uint16_t kDescriptorProbeUuid = 0xFEE3;
static constexpr uint16_t kLongLen = 244U;
static constexpr uint32_t kStatusEveryMs = 5000UL;

BLEService edgeService(kServiceUuid);
BLECharacteristic longReadChar(kLongReadUuid);
BLECharacteristic longWriteChar(kLongWriteUuid);
BLECharacteristic descriptorProbeChar(kDescriptorProbeUuid);

static uint8_t g_longReadValue[kLongLen];
static uint8_t g_longWriteInitial[kLongLen];
static volatile uint16_t g_lastWriteLen = 0U;
static volatile uint32_t g_writeCount = 0U;
static uint32_t g_lastStatusMs = 0UL;

static uint16_t checksum8(const uint8_t* data, uint16_t len) {
  uint16_t sum = 0U;
  for (uint16_t i = 0U; i < len; ++i) {
    sum = static_cast<uint16_t>((sum + data[i]) & 0xFFFFU);
  }
  return sum;
}

static void fillPattern(uint8_t* out, uint16_t len, uint8_t seed) {
  if (out == nullptr) {
    return;
  }
  for (uint16_t i = 0U; i < len; ++i) {
    out[i] = static_cast<uint8_t>(seed + i);
  }
}

static void writeCallback(uint16_t conn_hdl, BLECharacteristic* chr,
                          uint8_t* data, uint16_t len) {
  (void)conn_hdl;
  (void)chr;
  g_lastWriteLen = len;
  ++g_writeCount;

  Serial.print("write len=");
  Serial.print(len);
  Serial.print(" checksum=0x");
  Serial.println(checksum8(data, len), HEX);
}

static void connectCallback(uint16_t conn_hdl) {
  BLEConnection* conn = Bluefruit.Connection(conn_hdl);
  Serial.print("connected mtu=");
  Serial.print(conn != nullptr ? conn->getMtu() : 0U);
  Serial.print(" dle=");
  Serial.println(conn != nullptr ? conn->getDataLength() : 0U);
}

static void disconnectCallback(uint16_t conn_hdl, uint8_t reason) {
  (void)conn_hdl;
  Serial.print("disconnected reason=0x");
  Serial.print(reason, HEX);
  Serial.print(" ");
  Serial.println(Bluefruit.disconnectReasonName(reason));
}

static void startAdvertising() {
  Bluefruit.Advertising.clearData();
  Bluefruit.ScanResponse.clearData();
  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addService(edgeService);
  Bluefruit.Advertising.addName();
  Bluefruit.ScanResponse.addName();
  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.setInterval(80, 160);
  Bluefruit.Advertising.start(0);
}

void setup() {
  Serial.begin(115200);
  const uint32_t serialWaitStart = millis();
  while (!Serial && (millis() - serialWaitStart) < 1500UL) {
    delay(10);
  }

  fillPattern(g_longReadValue, sizeof(g_longReadValue), 0x00U);
  fillPattern(g_longWriteInitial, sizeof(g_longWriteInitial), 0x80U);

  Bluefruit.configPrphBandwidth(BANDWIDTH_MAX);
  Bluefruit.begin();
  Bluefruit.setName("X54-GATT-EDGE");
  Bluefruit.Periph.setConnectCallback(connectCallback);
  Bluefruit.Periph.setDisconnectCallback(disconnectCallback);

  edgeService.begin();

  longReadChar.setProperties(CHR_PROPS_READ | CHR_PROPS_NOTIFY);
  longReadChar.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
  longReadChar.setMaxLen(kLongLen);
  longReadChar.setUserDescriptor("244-byte long read");
  longReadChar.begin();
  longReadChar.write(g_longReadValue, sizeof(g_longReadValue));

  longWriteChar.setProperties(CHR_PROPS_READ | CHR_PROPS_WRITE |
                              CHR_PROPS_WRITE_WO_RESP);
  longWriteChar.setPermission(SECMODE_OPEN, SECMODE_OPEN);
  longWriteChar.setMaxLen(kLongLen);
  longWriteChar.setUserDescriptor("244-byte prepare write target");
  longWriteChar.setWriteCallback(writeCallback);
  longWriteChar.begin();
  longWriteChar.write(g_longWriteInitial, sizeof(g_longWriteInitial));

  descriptorProbeChar.setProperties(CHR_PROPS_READ | CHR_PROPS_WRITE);
  descriptorProbeChar.setPermission(SECMODE_OPEN, SECMODE_OPEN);
  descriptorProbeChar.setFixedLen(1);
  descriptorProbeChar.setUserDescriptor("Writable descriptor label");
  descriptorProbeChar.setPresentationFormatDescriptor(0x04, 0, 0x2700);
  descriptorProbeChar.setReportRefDescriptor(3, 1);
  descriptorProbeChar.begin();
  descriptorProbeChar.write8(0x5AU);

  startAdvertising();

  Serial.println("Advertising X54-GATT-EDGE");
  Serial.print("long_read_len=");
  Serial.println(kLongLen);
}

void loop() {
  const uint32_t now = millis();
  if ((now - g_lastStatusMs) < kStatusEveryMs) {
    delay(10);
    return;
  }
  g_lastStatusMs = now;

  uint8_t readback[kLongLen];
  const uint16_t len = longWriteChar.read(readback, sizeof(readback));
  Serial.print("status connected=");
  Serial.print(Bluefruit.connected() ? "yes" : "no");
  Serial.print(" writes=");
  Serial.print(static_cast<uint32_t>(g_writeCount));
  Serial.print(" last_write_len=");
  Serial.print(static_cast<uint16_t>(g_lastWriteLen));
  Serial.print(" write_value_len=");
  Serial.print(len);
  Serial.print(" checksum=0x");
  Serial.println(checksum8(readback, len), HEX);

  if (Bluefruit.connected() && longReadChar.notifyEnabled()) {
    (void)longReadChar.notify(g_longReadValue, sizeof(g_longReadValue));
  }
}
