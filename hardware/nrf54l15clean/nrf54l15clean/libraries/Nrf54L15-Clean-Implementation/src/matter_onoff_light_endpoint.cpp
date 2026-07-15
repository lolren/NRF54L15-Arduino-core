#if defined(NRF54L15_CLEAN_MATTER_CORE_ENABLE) && NRF54L15_CLEAN_MATTER_CORE_ENABLE
#include "matter_onoff_light_endpoint.h"

#include <string.h>

namespace xiao_nrf54l15 {

Nrf54MatterOnOffLightEndpoint::Nrf54MatterOnOffLightEndpoint(
    Nrf54MatterOnOffLightDevice* device)
    : device_(device) {}

void Nrf54MatterOnOffLightEndpoint::attach(Nrf54MatterOnOffLightDevice* device) {
  device_ = device;
}

void Nrf54MatterOnOffLightEndpoint::setAccessControl(MatterAccessControl* acl) {
  accessControl_ = acl;
}

bool Nrf54MatterOnOffLightEndpoint::hasAccessControl() const {
  return accessControl_ != nullptr;
}

void Nrf54MatterOnOffLightEndpoint::detach() { device_ = nullptr; }

bool Nrf54MatterOnOffLightEndpoint::attached() const {
  return device_ != nullptr;
}

bool Nrf54MatterOnOffLightEndpoint::snapshot(
    MatterOnOffLightDeviceState* outState) const {
  return device_ != nullptr && device_->snapshot(outState);
}

bool Nrf54MatterOnOffLightEndpoint::readAttribute(
    const MatterAttributePath& path, MatterAttributeValue* outValue,
    MatterInteractionStatus* outStatus) const {
  return readAttributeInternal(path, nullptr, outValue, outStatus);
}

bool Nrf54MatterOnOffLightEndpoint::readAttribute(
    const MatterAttributePath& path,
    const MatterAccessContext& accessContext,
    MatterAttributeValue* outValue,
    MatterInteractionStatus* outStatus) const {
  return readAttributeInternal(path, &accessContext, outValue, outStatus);
}

bool Nrf54MatterOnOffLightEndpoint::readAttributeInternal(
    const MatterAttributePath& path,
    const MatterAccessContext* accessContext,
    MatterAttributeValue* outValue,
    MatterInteractionStatus* outStatus) const {
  if (outValue == nullptr) {
    setStatus(outStatus, MatterInteractionStatus::kInvalidState);
    return false;
  }

  *outValue = MatterAttributeValue{};
  if (device_ == nullptr) {
    setStatus(outStatus, MatterInteractionStatus::kInvalidState);
    return false;
  }
  if (path.endpointId != kEndpointId) {
    setStatus(outStatus, MatterInteractionStatus::kUnsupportedEndpoint);
    return false;
  }
  if (accessContext != nullptr &&
      !accessAllowed(*accessContext, path.clusterId, path.endpointId,
                     AclPrivilege::kView)) {
    setStatus(outStatus, MatterInteractionStatus::kAccessDenied);
    return false;
  }

  switch (path.clusterId) {
    case kOnOffClusterId:
      if (path.attributeId == kOnOffAttributeId) {
        outValue->type = MatterAttributeValueType::kBool;
        const bool ok = device_->readOnOffAttribute(&outValue->boolValue);
        setStatus(outStatus, ok ? MatterInteractionStatus::kSuccess
                                : MatterInteractionStatus::kInvalidState);
        return ok;
      }
      if (path.attributeId == kGlobalSceneControlAttributeId) {
        outValue->type = MatterAttributeValueType::kBool;
        const bool ok =
            device_->readGlobalSceneControlAttribute(&outValue->boolValue);
        setStatus(outStatus, ok ? MatterInteractionStatus::kSuccess
                                : MatterInteractionStatus::kInvalidState);
        return ok;
      }
      setStatus(outStatus, MatterInteractionStatus::kUnsupportedAttribute);
      return false;

    case kIdentifyClusterId:
      if (path.attributeId == kIdentifyTimeAttributeId) {
        outValue->type = MatterAttributeValueType::kUint16;
        const bool ok =
            device_->readIdentifyTimeAttribute(&outValue->uint16Value);
        setStatus(outStatus, ok ? MatterInteractionStatus::kSuccess
                                : MatterInteractionStatus::kInvalidState);
        return ok;
      }
      setStatus(outStatus, MatterInteractionStatus::kUnsupportedAttribute);
      return false;

    default:
      setStatus(outStatus, MatterInteractionStatus::kUnsupportedCluster);
      return false;
  }
}

bool Nrf54MatterOnOffLightEndpoint::invokeCommand(
    const MatterCommandRequest& request, MatterCommandResult* outResult) {
  MatterAccessContext accessContext = {};
  accessContext.subjectId = request.subjectId;
  accessContext.fabricId = request.fabricId;
  accessContext.nodeId = request.nodeId;
  if (!accessAllowed(accessContext, request.path.clusterId,
                     request.path.endpointId, AclPrivilege::kOperate)) {
    fillResult(outResult, MatterInteractionStatus::kAccessDenied, false,
               false);
    return false;
  }
  return invokeCommandInternal(request, outResult);
}

bool Nrf54MatterOnOffLightEndpoint::invokeTrustedLocalCommand(
    const MatterCommandRequest& request, MatterCommandResult* outResult) {
  return invokeCommandInternal(request, outResult);
}

bool Nrf54MatterOnOffLightEndpoint::invokeCommandInternal(
    const MatterCommandRequest& request, MatterCommandResult* outResult) {
  if (device_ == nullptr) {
    fillResult(outResult, MatterInteractionStatus::kInvalidState, false, false);
    return false;
  }
  if (request.path.endpointId != kEndpointId) {
    fillResult(outResult, MatterInteractionStatus::kUnsupportedEndpoint, false,
               false);
    return false;
  }

  MatterOnOffLightDeviceState before = {};
  (void)device_->snapshot(&before);

  bool ok = false;
  MatterInteractionStatus status = MatterInteractionStatus::kUnsupportedCluster;
  switch (request.path.clusterId) {
    case kOnOffClusterId:
      status = MatterInteractionStatus::kUnsupportedCommand;
      switch (request.path.commandId) {
        case kOffCommandId:
          ok = device_->setOn(false, true);
          status = ok ? MatterInteractionStatus::kSuccess
                      : MatterInteractionStatus::kStorageFailure;
          break;
        case kOnCommandId:
          ok = device_->setOn(true, true);
          status = ok ? MatterInteractionStatus::kSuccess
                      : MatterInteractionStatus::kStorageFailure;
          break;
        case kToggleCommandId:
          ok = device_->toggle(true);
          status = ok ? MatterInteractionStatus::kSuccess
                      : MatterInteractionStatus::kStorageFailure;
          break;
        default:
          break;
      }
      break;

    case kIdentifyClusterId:
      if (request.path.commandId != kIdentifyCommandId) {
        fillResult(outResult, MatterInteractionStatus::kUnsupportedCommand,
                   false, false);
        return false;
      }
      if (!request.hasUint16Value) {
        fillResult(outResult, MatterInteractionStatus::kInvalidCommandData,
                   false, false);
        return false;
      }
      ok = device_->setIdentifyTimeSeconds(request.uint16Value);
      status = ok ? MatterInteractionStatus::kSuccess
                  : MatterInteractionStatus::kInvalidState;
      break;

    default:
      break;
  }

  MatterOnOffLightDeviceState after = {};
  (void)device_->snapshot(&after);
  const bool stateChanged =
      before.on != after.on || before.identifying != after.identifying ||
      before.identifyTimeSeconds != after.identifyTimeSeconds ||
      before.level != after.level;
  fillResult(outResult, status, ok, stateChanged);
  return ok;
}

const char* Nrf54MatterOnOffLightEndpoint::statusName(
    MatterInteractionStatus status) {
  switch (status) {
    case MatterInteractionStatus::kSuccess:
      return "success";
    case MatterInteractionStatus::kInvalidState:
      return "invalid-state";
    case MatterInteractionStatus::kUnsupportedEndpoint:
      return "unsupported-endpoint";
    case MatterInteractionStatus::kUnsupportedCluster:
      return "unsupported-cluster";
    case MatterInteractionStatus::kUnsupportedAttribute:
      return "unsupported-attribute";
    case MatterInteractionStatus::kUnsupportedCommand:
      return "unsupported-command";
    case MatterInteractionStatus::kInvalidCommandData:
      return "invalid-command-data";
    case MatterInteractionStatus::kStorageFailure:
      return "storage-failure";
    case MatterInteractionStatus::kAccessDenied:
      return "access-denied";
    default:
      return "unknown";
  }
}

const char* Nrf54MatterOnOffLightEndpoint::clusterName(
    MatterClusterId clusterId) {
  switch (clusterId) {
    case kOnOffClusterId:
      return "OnOff";
    case kLevelControlClusterId:
      return "LevelControl";
    case kIdentifyClusterId:
      return "Identify";
    default:
      return "UnknownCluster";
  }
}

const char* Nrf54MatterOnOffLightEndpoint::attributeName(
    MatterClusterId clusterId, uint32_t attributeId) {
  switch (clusterId) {
    case kOnOffClusterId:
      switch (attributeId) {
        case kOnOffAttributeId:
          return "OnOff";
        case kGlobalSceneControlAttributeId:
          return "GlobalSceneControl";
        default:
          return "UnknownOnOffAttribute";
      }
    case kLevelControlClusterId:
      switch (attributeId) {
        case Nrf54MatterOnOffLightDevice::kCurrentLevelAttributeId:
          return "CurrentLevel";
        case Nrf54MatterOnOffLightDevice::kMinLevelAttributeId:
          return "MinLevel";
        case Nrf54MatterOnOffLightDevice::kMaxLevelAttributeId:
          return "MaxLevel";
        default:
          return "UnknownLevelControlAttribute";
      }
    case kIdentifyClusterId:
      switch (attributeId) {
        case kIdentifyTimeAttributeId:
          return "IdentifyTime";
        default:
          return "UnknownIdentifyAttribute";
      }
    default:
      return "UnknownAttribute";
  }
}

const char* Nrf54MatterOnOffLightEndpoint::commandName(
    MatterClusterId clusterId, uint32_t commandId) {
  switch (clusterId) {
    case kOnOffClusterId:
      switch (commandId) {
        case kOffCommandId:
          return "Off";
        case kOnCommandId:
          return "On";
        case kToggleCommandId:
          return "Toggle";
        default:
          return "UnknownOnOffCommand";
      }
    case kLevelControlClusterId:
      switch (commandId) {
        case kMoveToLevelCommandId:
          return "MoveToLevel";
        case kMoveToLevelWithOnOffCommandId:
          return "MoveToLevelWithOnOff";
        default:
          return "UnknownLevelControlCommand";
      }
    case kIdentifyClusterId:
      switch (commandId) {
        case kIdentifyCommandId:
          return "Identify";
        default:
          return "UnknownIdentifyCommand";
      }
    default:
      return "UnknownCommand";
  }
}

void Nrf54MatterOnOffLightEndpoint::setStatus(
    MatterInteractionStatus* outStatus, MatterInteractionStatus status) {
  if (outStatus != nullptr) {
    *outStatus = status;
  }
}

void Nrf54MatterOnOffLightEndpoint::fillResult(
    MatterCommandResult* outResult, MatterInteractionStatus status,
    bool accepted, bool stateChanged) const {
  if (outResult == nullptr) {
    return;
  }

  outResult->status = status;
  outResult->accepted = accepted;
  outResult->stateChanged = stateChanged;
  if (device_ != nullptr) {
    (void)device_->snapshot(&outResult->light);
  } else {
    outResult->light = MatterOnOffLightDeviceState{};
  }
}

bool Nrf54MatterOnOffLightEndpoint::accessAllowed(
    const MatterAccessContext& accessContext,
    MatterClusterId clusterId,
    MatterEndpointId endpointId,
    AclPrivilege requiredPrivilege) const {
  if (!accessContext.complete()) {
    return false;
  }
  if (accessControl_ == nullptr) {
    return false;
  }
  return accessControl_->checkAccess(
      accessContext.subjectId, accessContext.fabricId, accessContext.nodeId,
      clusterId, endpointId, requiredPrivilege);
}

}  // namespace xiao_nrf54l15
#endif // NRF54L15_CLEAN_MATTER_CORE_ENABLE
