/*
  Dynamic GATT authorization diagnostic

  Service 0xFEC0
  - 0xFEC1: 244-byte authorized read value. Each read refreshes its counter.
  - 0xFEC2: authorized direct writes and controller-owned queued long writes.

  Direct writes beginning with 0xDE are rejected. Direct writes beginning with
  0xA5 are accepted with their first byte changed to 0x5A. Other direct writes
  are accepted unchanged. Prepare Write / Execute Write remains controller
  owned, so a completed long write increments the normal write count once but
  does not increment the write-authorization count.
*/

#include <bluefruit.h>

static constexpr uint16_t kServiceUuid = 0xFEC0U;
static constexpr uint16_t kReadUuid = 0xFEC1U;
static constexpr uint16_t kWriteUuid = 0xFEC2U;
static constexpr uint16_t kReadLength = 244U;
static constexpr uint16_t kWriteMaxLength = 512U;

BLEService authorizationService(kServiceUuid);
BLECharacteristic authorizedRead(kReadUuid);
BLECharacteristic authorizedWrite(kWriteUuid);

static uint8_t g_readValue[kReadLength];
static uint8_t g_writeReply[kWriteMaxLength];
static volatile uint32_t g_readAuthorizeCount = 0U;
static volatile uint32_t g_writeAuthorizeCount = 0U;
static volatile uint32_t g_writeCallbackCount = 0U;
static volatile uint16_t g_lastWriteLength = 0U;
static uint32_t g_lastStatusMs = 0U;

static void fillReadValue() {
  const uint32_t count = g_readAuthorizeCount;
  g_readValue[0] = static_cast<uint8_t>(count & 0xFFU);
  g_readValue[1] = static_cast<uint8_t>((count >> 8U) & 0xFFU);
  g_readValue[2] = static_cast<uint8_t>((count >> 16U) & 0xFFU);
  g_readValue[3] = static_cast<uint8_t>((count >> 24U) & 0xFFU);
  for (uint16_t i = 4U; i < sizeof(g_readValue); ++i) {
    g_readValue[i] = static_cast<uint8_t>(i);
  }
}

static void readAuthorizeCallback(uint16_t connHandle,
                                  BLECharacteristic* characteristic,
                                  ble_gatts_evt_read_t* request) {
  (void)characteristic;
  ++g_readAuthorizeCount;
  fillReadValue();

  ble_gatts_rw_authorize_reply_params_t reply{};
  reply.type = BLE_GATTS_AUTHORIZE_TYPE_READ;
  reply.params.read.gatt_status = BLE_GATT_STATUS_SUCCESS;
  reply.params.read.update = 1U;
  reply.params.read.offset = 0U;
  reply.params.read.len = sizeof(g_readValue);
  reply.params.read.p_data = g_readValue;
  const uint32_t status =
      sd_ble_gatts_rw_authorize_reply(connHandle, &reply);

  Serial.print("read authorize offset=");
  Serial.print(request->offset);
  Serial.print(" reply=");
  Serial.println(status);
}

static void writeAuthorizeCallback(uint16_t connHandle,
                                   BLECharacteristic* characteristic,
                                   ble_gatts_evt_write_t* request) {
  (void)characteristic;
  ++g_writeAuthorizeCount;

  ble_gatts_rw_authorize_reply_params_t reply{};
  reply.type = BLE_GATTS_AUTHORIZE_TYPE_WRITE;
  reply.params.write.offset = 0U;
  if (request->len > 0U && request->data[0] == 0xDEU) {
    reply.params.write.gatt_status =
        BLE_GATT_STATUS_ATTERR_INSUF_AUTHORIZATION;
    reply.params.write.update = 0U;
    reply.params.write.len = 0U;
    reply.params.write.p_data = nullptr;
  } else {
    const uint16_t length = min<uint16_t>(
        request->len, static_cast<uint16_t>(sizeof(g_writeReply)));
    if (length > 0U) {
      memcpy(g_writeReply, request->data, length);
      if (g_writeReply[0] == 0xA5U) {
        g_writeReply[0] = 0x5AU;
      }
    }
    reply.params.write.gatt_status = BLE_GATT_STATUS_SUCCESS;
    reply.params.write.update = 1U;
    reply.params.write.len = length;
    reply.params.write.p_data = g_writeReply;
  }

  const uint32_t status =
      sd_ble_gatts_rw_authorize_reply(connHandle, &reply);
  Serial.print("write authorize op=");
  Serial.print(request->op);
  Serial.print(" len=");
  Serial.print(request->len);
  Serial.print(" reply=");
  Serial.println(status);
}

static void writeCallback(uint16_t connHandle,
                          BLECharacteristic* characteristic, uint8_t* data,
                          uint16_t length) {
  (void)connHandle;
  (void)characteristic;
  (void)data;
  g_lastWriteLength = length;
  ++g_writeCallbackCount;
}

static void startAdvertising() {
  Bluefruit.Advertising.clearData();
  Bluefruit.ScanResponse.clearData();
  Bluefruit.Advertising.addFlags(
      BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addService(authorizationService);
  Bluefruit.Advertising.addName();
  Bluefruit.ScanResponse.addName();
  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.setInterval(80U, 160U);
  Bluefruit.Advertising.start(0U);
}

void setup() {
  Serial.begin(115200);
  const uint32_t serialStart = millis();
  while (!Serial && (millis() - serialStart) < 1500U) {
    delay(10);
  }

  fillReadValue();
  Bluefruit.configPrphBandwidth(BANDWIDTH_MAX);
  Bluefruit.begin();
  Bluefruit.setName("X54-GATT-AUTH");

  authorizationService.begin();

  authorizedRead.setProperties(CHR_PROPS_READ);
  authorizedRead.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
  authorizedRead.setMaxLen(kReadLength);
  authorizedRead.setReadAuthorizeCallback(readAuthorizeCallback);
  authorizedRead.begin();
  authorizedRead.write(g_readValue, sizeof(g_readValue));

  authorizedWrite.setProperties(CHR_PROPS_READ | CHR_PROPS_WRITE |
                                CHR_PROPS_WRITE_WO_RESP);
  authorizedWrite.setPermission(SECMODE_OPEN, SECMODE_OPEN);
  authorizedWrite.setMaxLen(kWriteMaxLength);
  authorizedWrite.setWriteAuthorizeCallback(writeAuthorizeCallback);
  authorizedWrite.setWriteCallback(writeCallback);
  authorizedWrite.begin();

  startAdvertising();
  Serial.println("GATT authorization diagnostic ready");
}

void loop() {
  if ((millis() - g_lastStatusMs) >= 5000U) {
    g_lastStatusMs = millis();
    Serial.print("read_auth=");
    Serial.print(g_readAuthorizeCount);
    Serial.print(" write_auth=");
    Serial.print(g_writeAuthorizeCount);
    Serial.print(" writes=");
    Serial.print(g_writeCallbackCount);
    Serial.print(" last_len=");
    Serial.println(g_lastWriteLength);
  }
  delay(10);
}
