#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "matter_onoff_light_endpoint.h"

using xiao_nrf54l15::AclEntry;
using xiao_nrf54l15::AclPrivilege;
using xiao_nrf54l15::MatterAccessContext;
using xiao_nrf54l15::MatterAccessControl;
using xiao_nrf54l15::MatterAttributePath;
using xiao_nrf54l15::MatterAttributeValue;
using xiao_nrf54l15::MatterCommandRequest;
using xiao_nrf54l15::MatterCommandResult;
using xiao_nrf54l15::MatterInteractionStatus;
using xiao_nrf54l15::MatterOnOffLightDeviceState;
using xiao_nrf54l15::Nrf54MatterOnOffLightDevice;
using xiao_nrf54l15::Nrf54MatterOnOffLightEndpoint;

namespace xiao_nrf54l15 {

bool Nrf54MatterOnOffLightDevice::snapshot(
    MatterOnOffLightDeviceState* outState) const {
  if (outState == nullptr) return false;
  *outState = MatterOnOffLightDeviceState{};
  return true;
}

bool Nrf54MatterOnOffLightDevice::readOnOffAttribute(bool* outOn) const {
  if (outOn == nullptr) return false;
  *outOn = false;
  return true;
}

bool Nrf54MatterOnOffLightDevice::readGlobalSceneControlAttribute(
    bool* outEnabled) const {
  if (outEnabled == nullptr) return false;
  *outEnabled = true;
  return true;
}

bool Nrf54MatterOnOffLightDevice::readIdentifyTimeAttribute(
    uint16_t* outSeconds) const {
  if (outSeconds == nullptr) return false;
  *outSeconds = 0U;
  return true;
}

bool Nrf54MatterOnOffLightDevice::setOn(bool, bool) { return true; }
bool Nrf54MatterOnOffLightDevice::toggle(bool) { return true; }
bool Nrf54MatterOnOffLightDevice::setIdentifyTimeSeconds(uint16_t) {
  return true;
}

}  // namespace xiao_nrf54l15

namespace {

AclEntry makeEntry(const uint8_t subject[8], const uint8_t fabric[8],
                   const uint8_t node[8], AclPrivilege privilege) {
  AclEntry entry = {};
  for (size_t i = 0; i < 8U; ++i) {
    entry.subjectId[i] = subject[i];
    entry.fabricId[i] = fabric[i];
    entry.nodeId[i] = node[i];
  }
  entry.wildcardCluster = true;
  entry.wildcardEndpoint = true;
  entry.privilege = privilege;
  entry.valid = true;
  return entry;
}

MatterAccessContext context(const uint8_t subject[8], const uint8_t fabric[8],
                            const uint8_t node[8]) {
  MatterAccessContext result = {};
  result.subjectId = subject;
  result.fabricId = fabric;
  result.nodeId = node;
  return result;
}

}  // namespace

int main() {
  const uint8_t subject[8] = {0x10, 1, 2, 3, 4, 5, 6, 7};
  const uint8_t otherSubject[8] = {0x11, 1, 2, 3, 4, 5, 6, 7};
  const uint8_t fabric[8] = {0x20, 1, 2, 3, 4, 5, 6, 7};
  const uint8_t node[8] = {0x30, 1, 2, 3, 4, 5, 6, 7};

  Nrf54MatterOnOffLightDevice device;
  Nrf54MatterOnOffLightEndpoint endpoint(&device);
  MatterAccessControl acl;
  assert(acl.addEntry(makeEntry(subject, fabric, node, AclPrivilege::kView)));
  endpoint.setAccessControl(&acl);

  MatterAttributePath path = {};
  path.clusterId = Nrf54MatterOnOffLightEndpoint::kOnOffClusterId;
  path.attributeId = Nrf54MatterOnOffLightEndpoint::kOnOffAttributeId;
  MatterAttributeValue value = {};
  MatterInteractionStatus status = MatterInteractionStatus::kInvalidState;

  // The legacy overload is a trusted local call and remains source compatible.
  assert(endpoint.readAttribute(path, &value, &status));
  assert(status == MatterInteractionStatus::kSuccess);

  assert(endpoint.readAttribute(path, context(subject, fabric, node), &value,
                                &status));
  assert(status == MatterInteractionStatus::kSuccess);

  Nrf54MatterOnOffLightEndpoint endpointWithoutAcl(&device);
  assert(!endpointWithoutAcl.readAttribute(
      path, context(subject, fabric, node), &value, &status));
  assert(status == MatterInteractionStatus::kAccessDenied);

  assert(!endpoint.readAttribute(path, context(otherSubject, fabric, node),
                                 &value, &status));
  assert(status == MatterInteractionStatus::kAccessDenied);

  MatterAccessContext incomplete = {};
  incomplete.fabricId = fabric;
  assert(!endpoint.readAttribute(path, incomplete, &value, &status));
  assert(status == MatterInteractionStatus::kAccessDenied);

  MatterCommandRequest request = {};
  request.path.clusterId = Nrf54MatterOnOffLightEndpoint::kIdentifyClusterId;
  request.path.commandId = Nrf54MatterOnOffLightEndpoint::kIdentifyCommandId;
  request.hasUint16Value = true;
  request.uint16Value = 2U;
  request.subjectId = subject;
  request.fabricId = fabric;
  request.nodeId = node;
  MatterCommandResult result = {};
  assert(!endpoint.invokeCommand(request, &result));
  assert(result.status == MatterInteractionStatus::kAccessDenied);

  assert(acl.addEntry(
      makeEntry(subject, fabric, node, AclPrivilege::kOperate)));
  assert(endpoint.invokeCommand(request, &result));
  assert(result.status == MatterInteractionStatus::kSuccess);

  request.subjectId = nullptr;
  request.fabricId = nullptr;
  assert(!endpoint.invokeCommand(request, &result));
  assert(result.status == MatterInteractionStatus::kAccessDenied);

  request.nodeId = nullptr;
  assert(!endpoint.invokeCommand(request, &result));
  assert(result.status == MatterInteractionStatus::kAccessDenied);

  // Local callers must cross an explicitly named trust boundary. The remote
  // API never interprets a missing authentication context as local access.
  assert(endpoint.invokeTrustedLocalCommand(request, &result));
  assert(result.status == MatterInteractionStatus::kSuccess);

  request.subjectId = subject;
  request.fabricId = fabric;
  request.nodeId = node;
  assert(!endpointWithoutAcl.invokeCommand(request, &result));
  assert(result.status == MatterInteractionStatus::kAccessDenied);
  request.subjectId = nullptr;
  request.fabricId = nullptr;
  request.nodeId = nullptr;
  assert(endpointWithoutAcl.invokeTrustedLocalCommand(request, &result));
  assert(result.status == MatterInteractionStatus::kSuccess);
  assert(strcmp(Nrf54MatterOnOffLightEndpoint::statusName(
                    MatterInteractionStatus::kAccessDenied),
                "access-denied") == 0);
  return 0;
}
