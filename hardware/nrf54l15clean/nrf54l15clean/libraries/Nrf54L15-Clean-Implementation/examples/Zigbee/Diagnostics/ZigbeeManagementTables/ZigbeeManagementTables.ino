#include <Arduino.h>

#include "nrf54l15_hal.h"
#include "zigbee_stack.h"

#if defined(NRF54L15_CLEAN_ZIGBEE_ENABLED) && (NRF54L15_CLEAN_ZIGBEE_ENABLED == 0)
#error "Enable Tools > Zigbee Support to build ZigbeeManagementTables."
#endif

using namespace xiao_nrf54l15;

// Demonstrates sketch-configured Zigbee ZDO management tables.
//
// The core does not claim automatic production route maintenance yet. For now,
// sketches can seed known neighbor/route rows so Mgmt_Lqi_req and Mgmt_Rtg_req
// return useful standard-form responses instead of empty tables.

static ZigbeeHomeAutomationDevice g_device;

static constexpr uint16_t kPanId = 0x1234U;
static constexpr uint16_t kLocalShort = 0x0101U;
static constexpr uint64_t kLocalIeee = 0x00124B0024ABC101ULL;
static constexpr uint64_t kExtendedPanId = 0x00124B0000001234ULL;
static constexpr uint64_t kParentIeee = 0x00124B0024ABC000ULL;

static void printHexByte(uint8_t value) {
  if (value < 0x10U) {
    Serial.print('0');
  }
  Serial.print(value, HEX);
}

static void printPayload(const char* label, uint16_t responseCluster,
                         const uint8_t* payload, uint8_t length) {
  Serial.print(label);
  Serial.print(" cluster=0x");
  Serial.print(responseCluster, HEX);
  Serial.print(" len=");
  Serial.print(length);
  Serial.print(" payload=");
  for (uint8_t i = 0U; i < length; ++i) {
    printHexByte(payload[i]);
    if ((i + 1U) < length) {
      Serial.print(' ');
    }
  }
  Serial.print("\r\n");
}

static void configureDevice() {
  ZigbeeBasicClusterConfig basic{};
  basic.manufacturerName = "nRF54";
  basic.modelIdentifier = "MgmtTables";
  basic.swBuildId = "dev";
  basic.powerSource = 0x01U;
  (void)g_device.configureOnOffLight(1U, kLocalIeee, kLocalShort, kPanId, basic,
                                     0x0000U,
                                     ZigbeeLogicalType::kRouter);

  ZigbeeNeighborTableEntry parent{};
  parent.extendedPanId = kExtendedPanId;
  parent.ieeeAddress = kParentIeee;
  parent.networkAddress = 0x0000U;
  parent.deviceType = ZigbeeLogicalType::kCoordinator;
  parent.rxOnWhenIdle = true;
  parent.relationship = ZigbeeNeighborRelationship::kParent;
  parent.permitJoin = ZigbeePermitJoinState::kAccepting;
  parent.depth = 0U;
  parent.lqi = 255U;
  (void)g_device.setNeighborTableEntry(0U, parent);

  ZigbeeRoutingTableEntry route{};
  route.destinationAddress = 0x0000U;
  route.status = ZigbeeRouteStatus::kActive;
  route.manyToOne = false;
  route.routeRecordRequired = false;
  route.nextHopAddress = 0x0000U;
  (void)g_device.setRoutingTableEntry(0U, route);
}

static void printManagementResponses() {
  uint16_t responseCluster = 0U;
  uint8_t payload[127] = {0};
  uint8_t length = 0U;

  const uint8_t lqiRequest[] = {0x31U, 0x00U};
  if (g_device.handleZdoRequest(kZigbeeZdoMgmtLqiRequest, lqiRequest,
                                sizeof(lqiRequest), &responseCluster, payload,
                                &length)) {
    printPayload("Mgmt_Lqi_rsp", responseCluster, payload, length);
  } else {
    Serial.print("Mgmt_Lqi_rsp failed\r\n");
  }

  const uint8_t rtgRequest[] = {0x32U, 0x00U};
  if (g_device.handleZdoRequest(kZigbeeZdoMgmtRtgRequest, rtgRequest,
                                sizeof(rtgRequest), &responseCluster, payload,
                                &length)) {
    printPayload("Mgmt_Rtg_rsp", responseCluster, payload, length);
  } else {
    Serial.print("Mgmt_Rtg_rsp failed\r\n");
  }
}

void setup() {
  Serial.begin(115200);
  const uint32_t startMs = millis();
  while (!Serial && (millis() - startMs) < 1500UL) {
    delay(10);
  }

  Serial.print("\r\nZigbeeManagementTables start\r\n");
  configureDevice();
  Serial.print("neighbors=");
  Serial.print(g_device.neighborTableCount());
  Serial.print(" routes=");
  Serial.print(g_device.routingTableCount());
  Serial.print("\r\n");
  printManagementResponses();
}

void loop() {
  delay(1000);
}
