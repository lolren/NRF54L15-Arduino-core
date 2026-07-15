// Matter On/Off Light Two-Board Demo over Thread
//
// Board A (Light Node):  ROLE = LIGHT_NODE
//   - Forms a Thread network (becomes Leader)
//   - Listens on Matter UDP port 5540 for CHIP-formatted commands
//   - Controls LED_BUILTIN based on on/off state
//   - Responds to: on, off, toggle, identify
//
// Board B (Controller):  ROLE = CONTROLLER
//   - Attaches to the same Thread network (becomes Child)
//   - Every 5 seconds sends a Matter command to the light node
//   - Cycles: on -> identify -> off -> toggle -> ...
//
// To use:
//   1. Flash Board A with ROLE = LIGHT_NODE
//   2. Flash Board B with ROLE = CONTROLLER
//   3. Power both boards
//   4. Board A LED toggles based on received commands
//   5. Serial monitor shows the full interaction


#include <nrf54_all.h>
#include "matter_device_attestation.h"
#include "matter_access_control.h"
#include "matter_fabric_table.h"
#include "matter_scenes.h"

#if !defined(NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE) || \
    (NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE == 0)
#error "Enable Tools > Thread Core > Experimental Stage Core before building."
#endif

// ═══════════════════════════════════════════════════════════════
// CHANGE THIS to switch roles: LIGHT_NODE or CONTROLLER
// ═══════════════════════════════════════════════════════════════
enum class DemoRole : uint8_t { LIGHT_NODE = 0, CONTROLLER = 1 };
constexpr DemoRole ROLE = DemoRole::LIGHT_NODE;
// ═══════════════════════════════════════════════════════════════

namespace {

using xiao_nrf54l15::Nrf54ThreadExperimental;

xiao_nrf54l15::MatterDeviceAttestation gAttestation;
xiao_nrf54l15::MatterAccessControl gAccessControl;
xiao_nrf54l15::MatterFabricTable gFabricTable;
xiao_nrf54l15::MatterScenes gScenes;
bool gAttestationReady = false;
bool gAccessControlReady = false;
bool gFabricTableReady = false;

// Matter protocol constants
constexpr uint16_t kMatterUdpPort = 5540U;
constexpr uint16_t kProtocolSecureChannel = 0x0000U;
constexpr uint16_t kProtocolInteractionModel = 0x0001U;

// Interaction Model opcodes
constexpr uint8_t kOpReadRequest = 0x02U;
constexpr uint8_t kOpReportData = 0x05U;
constexpr uint8_t kOpInvokeCommandRequest = 0x08U;
constexpr uint8_t kOpInvokeCommandResponse = 0x09U;

// On/Off cluster IDs
constexpr uint32_t kOnOffClusterId = 0x0006U;
constexpr uint32_t kLevelControlClusterId = 0x0008U;
constexpr uint32_t kIdentifyClusterId = 0x0003U;
constexpr uint32_t kOffCommandId = 0x00U;
constexpr uint32_t kOnCommandId = 0x01U;
constexpr uint32_t kToggleCommandId = 0x02U;
constexpr uint32_t kIdentifyCommandId = 0x00U;
constexpr uint32_t kMoveToLevelCommandId = 0x00U;
constexpr uint32_t kMoveToLevelWithOnOffCommandId = 0x04U;
constexpr uint16_t kScenesClusterId = xiao_nrf54l15::kSceneClusterId;
constexpr uint32_t kAddSceneCommandId = 0x00U;
constexpr uint32_t kViewSceneCommandId = 0x01U;
constexpr uint32_t kRemoveSceneCommandId = 0x02U;
constexpr uint32_t kRecallSceneCommandId = 0x03U;
constexpr uint32_t kStoreSceneCommandId = 0x04U;

constexpr uint32_t kReportIntervalMs = 3000U;
constexpr uint32_t kCommandIntervalMs = 5000U;
constexpr uint32_t kDiscoveryIntervalMs = 3000U;
constexpr uint16_t kControllerDiscoveryPort = kMatterUdpPort + 1U;
constexpr uint8_t kLightAnnouncePayload = 0xAAU;

static const otIp6Address kMeshLocalAllNodes = {
  .mFields = {
    .m8 = {0xff, 0x03, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1}
  }
};

Nrf54ThreadExperimental gThread;
bool gLightOn = false;
bool gIdentifying = false;
uint32_t gIdentifyEndMs = 0U;
uint32_t gLastReportMs = 0U;
uint32_t gLastCommandMs = 0U;
uint32_t gLastDiscoveryMs = 0U;
uint8_t gCommandSequence = 0U;
uint8_t gBrightness = 255U; // 0..255 brightness (PWM)
Nrf54ThreadExperimental::Role gLastRole =
    Nrf54ThreadExperimental::Role::kUnknown;
otIp6Address gLightNodeAddr = {};
bool gLightNodeKnown = false;

// ─── CHIP Message Framing ───────────────────────────────────────

void writeUint16Le(uint16_t value, uint8_t* out, size_t offset) {
  out[offset]     = static_cast<uint8_t>(value & 0xFFU);
  out[offset + 1] = static_cast<uint8_t>((value >> 8U) & 0xFFU);
}

void writeUint32Le(uint32_t value, uint8_t* out, size_t offset) {
  out[offset]     = static_cast<uint8_t>(value & 0xFFU);
  out[offset + 1] = static_cast<uint8_t>((value >> 8U) & 0xFFU);
  out[offset + 2] = static_cast<uint8_t>((value >> 16U) & 0xFFU);
  out[offset + 3] = static_cast<uint8_t>((value >> 24U) & 0xFFU);
}

uint16_t readUint16Le(const uint8_t* data, size_t offset) {
  return static_cast<uint16_t>(data[offset]) |
         (static_cast<uint16_t>(data[offset + 1]) << 8U);
}

uint32_t readUint32Le(const uint8_t* data, size_t offset) {
  return static_cast<uint32_t>(data[offset]) |
         (static_cast<uint32_t>(data[offset + 1]) << 8U) |
         (static_cast<uint32_t>(data[offset + 2]) << 16U) |
         (static_cast<uint32_t>(data[offset + 3]) << 24U);
}

// Build a CHIP message header (unencrypted, for commissioning/test)
// Returns header length in bytes
size_t buildChipHeader(uint8_t* buffer, size_t capacity,
                       uint8_t exchangeFlags,
                       uint16_t messageId, uint16_t exchangeId,
                       uint16_t protocolId, uint8_t protocolOpcode) {
  if (capacity < 20U) return 0U;
  size_t off = 0U;
  buffer[off++] = exchangeFlags;      // Exchange Flags
  buffer[off++] = 0U;                 // Session Type (unsecured)
  buffer[off++] = 0U;                 // Security Flags
  writeUint16Le(messageId, buffer, off); off += 2U;
  writeUint32Le(0U, buffer, off); off += 4U;       // Source Node ID
  writeUint32Le(0U, buffer, off); off += 4U;       // Dest Node ID
  writeUint16Le(exchangeId, buffer, off); off += 2U;
  writeUint16Le(0U, buffer, off); off += 2U;       // Vendor ID (standard)
  writeUint16Le(protocolId, buffer, off); off += 2U;
  buffer[off++] = protocolOpcode;
  return off;
}

// Parse a CHIP message header. Returns payload offset.
bool parseChipHeader(const uint8_t* data, uint16_t length,
                     uint16_t* outProtocolId, uint8_t* outOpcode,
                     uint16_t* outExchangeId, size_t* outPayloadOffset) {
  if (length < 20U) return false;
  size_t off = 3U;                                   // skip flags+sess+sec
  uint16_t msgId = readUint16Le(data, off); off += 2U;  (void)msgId;
  off += 8U;                                         // skip node IDs
  if (outExchangeId) *outExchangeId = readUint16Le(data, off);
  off += 2U;
  off += 2U;                                         // skip vendor ID
  if (outProtocolId) *outProtocolId = readUint16Le(data, off);
  off += 2U;
  if (outOpcode) *outOpcode = data[off];
  off += 1U;
  if (outPayloadOffset) *outPayloadOffset = off;
  return true;
}

// ─── Matter Command Builder (Controller side) ───────────────────

static uint16_t sMsgId = 0U;
static uint16_t sExchangeId = 0x1234U;

uint16_t nextMsgId() {
  sMsgId++;
  if (sMsgId == 0U) sMsgId = 1U;
  return sMsgId;
}

uint16_t nextExchangeId() {
  sExchangeId++;
  if (sExchangeId == 0U) sExchangeId = 1U;
  return sExchangeId;
}

// Build and send a Matter InvokeCommandRequest for On/Off cluster
bool sendMatterCommand(const otIp6Address& peerAddr, uint16_t peerPort,
                       uint32_t clusterId, uint32_t commandId) {
  uint8_t buf[128] = {0};

  // CHIP IM InvokeCommandRequest payload:
  //   SuppressResponse (bool, 1 byte)
  //   TimedRequest (bool, 1 byte)  
  //   InvokeRequests (list)
  //     CommandPathIB: EndpointId (uint16), ClusterId (uint32), CommandId (uint32)
  //     CommandDataIB: (empty for on/off/toggle)
  size_t off = 0U;
  buf[off++] = 0U;  // SuppressResponse = false
  buf[off++] = 0U;  // TimedRequest = false

  // CommandPathIB
  writeUint16Le(1U, buf, off); off += 2U;           // EndpointId = 1
  writeUint32Le(clusterId, buf, off); off += 4U;    // ClusterId
  writeUint32Le(commandId, buf, off); off += 4U;    // CommandId

  // CommandDataIB: empty (needs a null TLV to terminate)
  buf[off++] = 0x18U;  // end-of-container TLV
  buf[off++] = 0x18U;  // end-of-container TLV (second for outer list)

  // Build CHIP header + payload
  uint8_t frame[256] = {0};
  uint16_t exchangeId = nextExchangeId();
  size_t headerLen = buildChipHeader(
      frame, sizeof(frame),
      0x05U,  // Initiator + Reliable
      nextMsgId(), exchangeId,
      kProtocolInteractionModel, kOpInvokeCommandRequest);

  if (headerLen == 0U) return false;
  memcpy(&frame[headerLen], buf, off);

  return gThread.sendUdp(peerAddr, peerPort, frame,
                         static_cast<uint16_t>(headerLen + off));
}

// ─── Matter Command Handler (Light Node side) ───────────────────

void handleInvokeCommand(const uint8_t* payload, uint16_t length) {
  if (payload == nullptr || length < 14U) return;

  // Parse CommandPathIB
  size_t off = 2U;  // skip SuppressResponse + TimedRequest
  uint16_t endpointId = readUint16Le(payload, off); off += 2U;
  uint32_t clusterId  = readUint32Le(payload, off); off += 4U;
  uint32_t commandId  = readUint32Le(payload, off); off += 4U;

  Serial.print("matter_light cmd endpoint=");
  Serial.print(endpointId);
  Serial.print(" cluster=0x");
  Serial.print(clusterId, HEX);
  Serial.print(" command=0x");
  Serial.print(commandId, HEX);

  // ACL check: deny if access control is configured and subject is not authorized
  // ACL check: verify subject has operate privilege
  if (gAccessControl.entryCount() > 0U) {
    uint8_t defaultId[8] = {0};
    if (!gAccessControl.checkAccess(defaultId, defaultId, defaultId,
                                    clusterId, endpointId,
                                    xiao_nrf54l15::AclPrivilege::kOperate)) {
      Serial.println(" -> ACL_DENIED");
      return;
    }
  }

  if (clusterId == kOnOffClusterId) {
    if (commandId == kOnCommandId) {
      gLightOn = true;
      Serial.println(" -> ON");
    } else if (commandId == kOffCommandId) {
      gLightOn = false;
      Serial.println(" -> OFF");
    } else if (commandId == kToggleCommandId) {
      gLightOn = !gLightOn;
      Serial.print(" -> TOGGLE (now ");
      Serial.print(gLightOn ? "ON" : "OFF");
      Serial.println(")");
    } else {
      Serial.println(" -> UNKNOWN");
    }
  } else if (clusterId == kLevelControlClusterId) {
    if (commandId == kMoveToLevelCommandId ||
        commandId == kMoveToLevelWithOnOffCommandId) {
      uint8_t level = payload[off];
      gLightOn = (level > 0U);
      gBrightness = level;
      Serial.print(" -> MOVE_TO_LEVEL ");
      Serial.println(level);
    } else {
      Serial.println(" -> UNKNOWN");
    }
  } else if (clusterId == kIdentifyClusterId) {
    if (commandId == kIdentifyCommandId) {
      gIdentifying = true;
      gIdentifyEndMs = millis() + 5000UL;
      Serial.println(" -> IDENTIFY (5s)");
    } else {
      Serial.println(" -> UNKNOWN");
    }
  } else if (clusterId == kScenesClusterId) {
    if (commandId == kAddSceneCommandId) {
      // Parse: groupId(2) + sceneId(1) + name(16) + onOff(1) + level(1)
      uint16_t sceneGroupId = readUint16Le(payload, off); off += 2U;
      uint8_t sceneId = payload[off++];
      char sname[17] = {0};
      uint8_t nameLen = (off + 16 <= length) ? 16U : (length - off);
      memcpy(sname, &payload[off], nameLen); off += 16;
      uint8_t sceneOnOff = (off < length) ? payload[off++] : 0U;
      uint8_t sceneLevel = (off < length) ? payload[off] : 255U;

      xiao_nrf54l15::SceneExtension ext = {};
      ext.on = (sceneOnOff != 0U);
      ext.level = sceneLevel;
      bool ok = gScenes.addScene(sceneGroupId, sceneId, sname, ext);
      Serial.print(" -> ADD_SCENE ");
      Serial.print(sname);
      Serial.print(ok ? " OK" : " FAIL");
      Serial.println();
    } else if (commandId == kRecallSceneCommandId) {
      uint16_t sceneGroupId = readUint16Le(payload, off); off += 2U;
      uint8_t sceneId = payload[off++];
      xiao_nrf54l15::SceneExtension ext = {};
      bool ok = gScenes.recallScene(sceneGroupId, sceneId, &ext);
      if (ok) {
        gLightOn = ext.on;
        gBrightness = ext.level;
        Serial.print(" -> RECALL_SCENE on=");
        Serial.print(ext.on ? 1 : 0);
        Serial.print(" level=");
        Serial.println(ext.level);
      } else {
        Serial.println(" -> RECALL_SCENE FAIL");
      }
    } else if (commandId == kViewSceneCommandId) {
      uint16_t sceneGroupId = readUint16Le(payload, off); off += 2U;
      uint8_t sceneId = payload[off++];
      xiao_nrf54l15::SceneEntry entry = {};
      bool ok = gScenes.viewScene(sceneGroupId, sceneId, &entry);
      if (ok) {
        Serial.print(" -> VIEW_SCENE ");
        Serial.print(entry.name);
        Serial.print(" on=");
        Serial.print(entry.extensions.on ? 1 : 0);
        Serial.print(" level=");
        Serial.println(entry.extensions.level);
      } else {
        Serial.println(" -> VIEW_SCENE FAIL");
      }
    } else {
      Serial.println(" -> UNKNOWN");
    }
  } else {
    Serial.println(" -> UNSUPPORTED_CLUSTER");
  }
}

void handleReadRequest(const uint8_t* payload, uint16_t length) {
  if (payload == nullptr || length < 6U) return;

  // Parse attribute path
  size_t off = 0U;
  uint16_t endpointId = readUint16Le(payload, off); off += 2U;
  uint32_t clusterId  = readUint32Le(payload, off); off += 4U;
  // uint32_t attributeId = readUint32Le(payload, off); off += 4U;

  Serial.print("matter_light read endpoint=");
  Serial.print(endpointId);
  Serial.print(" cluster=0x");
  Serial.println(clusterId, HEX);
}

// ─── UDP Receive Handler ────────────────────────────────────────

void onUdpReceive(void*, const uint8_t* payload, uint16_t length,
                  const otMessageInfo& info) {
  if (payload == nullptr) return;

  if (ROLE == DemoRole::CONTROLLER && length == 1U &&
      payload[0] == kLightAnnouncePayload) {
    memcpy(&gLightNodeAddr, &info.mPeerAddr, sizeof(gLightNodeAddr));
    if (!gLightNodeKnown) {
      Serial.println("matter_light discovered light node");
    }
    gLightNodeKnown = true;
    return;
  }

  if (length < 20U) return;

  uint16_t protocolId = 0U;
  uint8_t opcode = 0U;
  uint16_t exchangeId = 0U;
  size_t payloadOffset = 0U;

  if (!parseChipHeader(payload, length, &protocolId, &opcode,
                       &exchangeId, &payloadOffset)) {
    return;
  }

  const uint16_t appLen = static_cast<uint16_t>(
      length > payloadOffset ? length - payloadOffset : 0U);
  const uint8_t* appPayload = appLen > 0U ? &payload[payloadOffset] : nullptr;

  Serial.print("matter_light rx protocol=0x");
  Serial.print(protocolId, HEX);
  Serial.print(" op=0x");
  Serial.print(opcode, HEX);
  Serial.print(" len=");
  Serial.print(appLen);

  if (protocolId == kProtocolInteractionModel) {
    switch (opcode) {
      case kOpInvokeCommandRequest:
        Serial.print(" [InvokeCommandRequest] ");
        handleInvokeCommand(appPayload, appLen);
        break;
      case kOpReadRequest:
        Serial.print(" [ReadRequest] ");
        handleReadRequest(appPayload, appLen);
        break;
      default:
        Serial.println(" [unknown IM op]");
        break;
    }
  } else {
    Serial.println(" [non-IM protocol]");
  }
}

void printDacStatus() {
  xiao_nrf54l15::AttestationCertificate dac;
  xiao_nrf54l15::AttestationCertificate pai;
  xiao_nrf54l15::AttestationCertificate paa;
  Serial.print("dac_status available=");
  Serial.print(gAttestation.available() ? 1 : 0);
  if (gAttestation.getDAC(&dac)) {
    Serial.print(" vendor=0x");
    Serial.print(dac.vendorId, HEX);
    Serial.print(" product=0x");
    Serial.print(dac.productId, HEX);
    Serial.print(" type=");
    Serial.print(xiao_nrf54l15::MatterDeviceAttestation::certTypeName(dac.type));
  }
  if (gAttestation.verifyChain(dac, pai, paa)) {
    Serial.print(" chain=valid");
  } else {
    Serial.print(" chain=invalid");
  }
  Serial.println();
}

void printAclStatus() {
  Serial.print("acl entries=");
  Serial.print(gAccessControl.entryCount());
  for (uint8_t i = 0; i < gAccessControl.entryCount(); i++) {
    xiao_nrf54l15::AclEntry entry = {};
    if (gAccessControl.getEntry(i, &entry)) {
      Serial.print(" [");
      Serial.print(i);
      Serial.print("] priv=");
      Serial.print(xiao_nrf54l15::MatterAccessControl::privilegeName(entry.privilege));
      Serial.print(" cluster=0x");
      Serial.print(entry.clusterId, HEX);
      if (entry.wildcardFabric) Serial.print(" wild-fabric");
      if (entry.wildcardNode) Serial.print(" wild-node");
      if (entry.wildcardSubject) Serial.print(" wild-subject");
      if (entry.wildcardCluster) Serial.print(" wild-cluster");
      if (entry.wildcardEndpoint) Serial.print(" wild-ep");
    }
  }
  Serial.println();
}

void printFabricStatus() {
  Serial.print("fabrics count=");
  Serial.print(gFabricTable.fabricCount());
  for (uint8_t i = 0; i < xiao_nrf54l15::kMaxFabrics; i++) {
    xiao_nrf54l15::FabricEntry fe = {};
    if (gFabricTable.getFabric(i, &fe)) {
      Serial.print(" [");
      Serial.print(i);
      Serial.print("] id=0x");
      Serial.print(fe.fabricId[7], HEX);
      Serial.print(" node=0x");
      Serial.print(fe.nodeId[7], HEX);
      Serial.print(" ");
      Serial.print(fe.label);
      if (fe.isLocalFabric) Serial.print(" local");
    }
  }
  Serial.println();
}

// ─── LED Control ────────────────────────────────────────────────

void applyLed() {
#if defined(LED_BUILTIN)
  if (gIdentifying) {
    // Blink during identify - use PWM for smooth blink
    const uint8_t blinkLevel = ((millis() / 150UL) & 0x1UL) ? gBrightness : 0U;
    analogWrite(LED_BUILTIN, 255U - blinkLevel);  // active-low LED
    return;
  }
  // Use PWM for brightness
  const uint8_t outLevel = gLightOn ? gBrightness : 0U;
  analogWrite(LED_BUILTIN, 255U - outLevel);  // active-low
#endif
}

// ─── Status Reporting ────────────────────────────────────────────

void printStatus() {
  Serial.print("matter_light role=");
  Serial.print(gThread.roleName());
  Serial.print(" rloc16=0x");
  Serial.print(gThread.rloc16(), HEX);
  Serial.print(" on=");
  Serial.print(gLightOn ? 1 : 0);
  Serial.print(" identifying=");
  Serial.print(gIdentifying ? 1 : 0);
  Serial.print(" brightness=");
  Serial.print(gBrightness);
  Serial.print(" light_known=");
  Serial.print(gLightNodeKnown ? 1 : 0);
  Serial.print(" my_role=");
  Serial.print(ROLE == DemoRole::LIGHT_NODE ? "light_node" : "controller");
  Serial.print(" attestation=");
  Serial.print(gAttestation.available() ? 1 : 0);
  Serial.print(" fabrics=");
  Serial.print(gFabricTable.fabricCount());
  Serial.print(" acl_entries=");
  Serial.print(gAccessControl.entryCount());
  Serial.println();
}

void announceLightNode() {
  if (ROLE != DemoRole::LIGHT_NODE || !gThread.udpOpened(kMatterUdpPort)) {
    return;
  }
  if ((millis() - gLastDiscoveryMs) < kDiscoveryIntervalMs) {
    return;
  }
  gLastDiscoveryMs = millis();
  const uint8_t payload[1] = {kLightAnnouncePayload};
  (void)gThread.sendUdp(kMeshLocalAllNodes, kControllerDiscoveryPort,
                        payload, sizeof(payload));
}

}  // namespace

// ─── Setup ──────────────────────────────────────────────────────

void setup() {
  Serial.begin(115200);
  Serial1.begin(115200);
  // Serial may not connect on nRF54L15 - do not wait

#if defined(LED_BUILTIN)
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);
#endif

  analogWriteResolution(8);  // 8-bit PWM for LED
#if defined(LED_BUILTIN)
  pinMode(LED_BUILTIN, OUTPUT);
#endif

  Serial.println();
  Serial.print("matter_light === Matter On/Off Light 2-Board Demo ===");
  Serial.println();
  Serial.print("matter_light role=");
  Serial.println(ROLE == DemoRole::LIGHT_NODE ? "LIGHT_NODE" : "CONTROLLER");

  // Use demo dataset so both boards share the same Thread network
  otOperationalDataset dataset = {};
  Nrf54ThreadExperimental::buildDemoDataset(&dataset);
  gThread.setActiveDataset(dataset);
  if (ROLE == DemoRole::LIGHT_NODE) {
    gThread.beginAsRouter();
  } else {
    gThread.beginAsChild();
  }

  Serial.print("matter_light thread_begin=");
  Serial.println(gThread.started() ? 1 : 0);

  if (ROLE == DemoRole::LIGHT_NODE) {
    // Light node: open UDP listener for Matter commands
    const bool udpOk = gThread.openUdp(kMatterUdpPort, onUdpReceive, nullptr);
    Serial.print("matter_light udp_open=");
    Serial.println(udpOk ? 1 : 0);
    if (!udpOk) {
      Serial.print("matter_light udp_error=");
      Serial.println(static_cast<int>(gThread.lastUdpError()));
    }
    Serial.println("matter_light Listening for Matter commands on UDP port 5540...");
  } else {
    const bool udpOk =
        gThread.openUdp(kControllerDiscoveryPort, onUdpReceive, nullptr);
    Serial.print("matter_light controller_udp_open=");
    Serial.println(udpOk ? 1 : 0);
    Serial.println("matter_light Waiting for light-node discovery...");
  }

  // Initialize DAC attestation on light node
  if (ROLE == DemoRole::LIGHT_NODE) {
    const uint16_t vendorId = 0xFFF2U;  // Test vendor
    const uint16_t productId = 0x8001U; // Test product
    uint8_t serialNumber[32] = {0};
    memcpy(serialNumber, "NRF54L15-2BTEST", 15);
    gAttestationReady = gAttestation.generateTestChain(vendorId, productId, serialNumber);
    Serial.print("matter_light dac_init=");
    Serial.println(gAttestationReady ? 1 : 0);

    // Initialize ACL with default view access + operate entries
    gAccessControl.addDefaultViewEntry();

    // Initialize fabric table with test fabrics
    gFabricTable.addTestFabric(0, "Fabric-1");  // Fabric for controller
    gFabricTable.addTestFabric(1, "Fabric-2");  // Simulated second fabric

    // Add per-fabric ACL entries (demonstrates multi-fabric capability)
    xiao_nrf54l15::FabricEntry f1 = {}, f2 = {};
    if (gFabricTable.getFabric(0, &f1)) {
      gAccessControl.addNodeOperateEntry(nullptr, f1.fabricId);  // Fabric-1 only
    }
    if (gFabricTable.getFabric(1, &f2)) {
      gAccessControl.addNodeOperateEntry(nullptr, f2.fabricId);  // Fabric-2 only
    }

    // Also add wildcard operate entry (for testing with default fabric ID)
    gAccessControl.addNodeOperateEntry(nullptr, nullptr);  // Any fabric

    gFabricTableReady = true;
    gAccessControlReady = true;
    Serial.println("matter_light acl_init=1");
  }

  if (ROLE == DemoRole::LIGHT_NODE) {
    printDacStatus();
    printFabricStatus();
    printAclStatus();
  }

  printStatus();
}

// ─── Loop ───────────────────────────────────────────────────────

void loop() {
  gThread.process();
  applyLed();
  announceLightNode();

  // Check identify timeout
  if (gIdentifying && millis() >= gIdentifyEndMs) {
    gIdentifying = false;
  }

  const Nrf54ThreadExperimental::Role currentRole = gThread.role();
  if (currentRole != gLastRole) {
    gLastRole = currentRole;
    printStatus();
  }

  // ─── Controller: send commands to light node ──────────────
  if (ROLE == DemoRole::CONTROLLER && gThread.attached() && gLightNodeKnown) {
    if ((millis() - gLastCommandMs) >= kCommandIntervalMs) {
      gLastCommandMs = millis();

      // 8-command cycle: ON, MOVE_TO_LEVEL(64), OFF, TOGGLE, IDENTIFY,
      // MOVE_TO_LEVEL(192), ADD_SCENE(dim), RECALL_SCENE(dim)
      const uint8_t seqMod = gCommandSequence % 8U;
      const uint32_t clusterId = [](uint8_t s) -> uint32_t {
        switch (s) {
          case 0:  return kOnOffClusterId;
          case 1:  return kLevelControlClusterId;
          case 2:  return kOnOffClusterId;
          case 3:  return kOnOffClusterId;
          case 4:  return kIdentifyClusterId;
          case 5:  return kLevelControlClusterId;
          case 6:  return kScenesClusterId;
          default: return kScenesClusterId;
        }
      }(seqMod);
      const uint32_t commandId = [](uint8_t s) -> uint32_t {
        switch (s) {
          case 0:  return kOnCommandId;
          case 1:  return kMoveToLevelCommandId;
          case 2:  return kOffCommandId;
          case 3:  return kToggleCommandId;
          case 4:  return kIdentifyCommandId;
          case 5:  return kMoveToLevelCommandId;
          case 6:  return kAddSceneCommandId;
          default: return kRecallSceneCommandId;
        }
      }(seqMod);

      const uint8_t levelValue = (clusterId == kLevelControlClusterId)
          ? (seqMod == 1U ? 64U : 192U)
          : 255U;

      Serial.print("matter_light sending cmd=0x");
      Serial.print(commandId, HEX);
      if (clusterId == kLevelControlClusterId) {
        Serial.print(" level=");
        Serial.print(levelValue);
      }
      if (clusterId == kScenesClusterId) {
        Serial.print(" scene=");
        Serial.print(seqMod == 6U ? "add" : "recall");
      }
      Serial.print(" to light... ");

      // Build payload
      bool sent = false;
      uint8_t buf[128] = {0};
      size_t off = 0U;
      buf[off++] = 0U;  // SuppressResponse
      buf[off++] = 0U;  // TimedRequest
      writeUint16Le(1U, buf, off); off += 2U;
      writeUint32Le(clusterId, buf, off); off += 4U;
      writeUint32Le(commandId, buf, off); off += 4U;

      if (clusterId == kLevelControlClusterId) {
        buf[off++] = levelValue;
        writeUint16Le(0U, buf, off); off += 2U;
      } else if (clusterId == kScenesClusterId && commandId == kAddSceneCommandId) {
        writeUint16Le(0U, buf, off); off += 2U;  // groupId=0
        buf[off++] = 1U;  // sceneId=1
        memcpy(&buf[off], "DimScene", 8); off += 16;  // name
        buf[off++] = 1U;  // on=true
        buf[off++] = 64U; // level=64
      } else if (clusterId == kScenesClusterId && commandId == kRecallSceneCommandId) {
        writeUint16Le(0U, buf, off); off += 2U;  // groupId=0
        buf[off++] = 1U;  // sceneId=1
      }
      buf[off++] = 0x18U;
      buf[off++] = 0x18U;

      uint8_t frame[256] = {0};
      size_t headerLen = buildChipHeader(
          frame, sizeof(frame), 0x05U, nextMsgId(),
          nextExchangeId(), kProtocolInteractionModel,
          kOpInvokeCommandRequest);
      if (headerLen > 0U) {
        memcpy(&frame[headerLen], buf, off);
        sent = gThread.sendUdp(gLightNodeAddr, kMatterUdpPort, frame,
                               static_cast<uint16_t>(headerLen + off));
      }
      Serial.println(sent ? "OK" : "FAIL");
      gCommandSequence++;
    }
  }

  // Periodic status
  if ((millis() - gLastReportMs) >= kReportIntervalMs) {
    gLastReportMs = millis();
    printStatus();
  }
}
