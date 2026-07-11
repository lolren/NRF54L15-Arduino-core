#if defined(NRF54L15_CLEAN_MATTER_CORE_ENABLE) && NRF54L15_CLEAN_MATTER_CORE_ENABLE
#include "matter_onoff_light.h"

#include <Arduino.h>
#include <string.h>

namespace xiao_nrf54l15 {
namespace {

uint16_t remainingIdentifySeconds(uint32_t identifyEndMs) {
  if (identifyEndMs == 0U) {
    return 0U;
  }

  const int32_t deltaMs = static_cast<int32_t>(identifyEndMs - millis());
  if (deltaMs <= 0) {
    return 0U;
  }

  const uint32_t unsignedDeltaMs = static_cast<uint32_t>(deltaMs);
  return static_cast<uint16_t>((unsignedDeltaMs + 999U) / 1000U);
}

}  // namespace

bool Nrf54MatterOnOffLightDevice::begin(const char* storageNamespace,
                                        bool restoreState) {
  if (storageOpen_) {
    return false;
  }

  const char* targetNamespace =
      (storageNamespace != nullptr && storageNamespace[0] != '\0')
          ? storageNamespace
          : "matter_onoff";
  if (!prefs_.begin(targetNamespace, false)) {
    return false;
  }

  storageOpen_ = true;
  on_ = false;
  globalSceneControl_ = true;
  identifyEndMs_ = 0U;
  startUpBehavior_ = MatterOnOffLightStartUpBehavior::kRestorePrevious;

  MatterOnOffLightPersistentState persistentState = {};
  bool previousOn = false;
  if (restoreState && loadPersistentState(&persistentState)) {
    previousOn = (persistentState.flags & kPersistentFlagOn) != 0U;
    startUpBehavior_ = static_cast<MatterOnOffLightStartUpBehavior>(
        persistentState.startUpBehavior);
  }

  if (!applyStartUpBehavior(previousOn)) {
    return false;
  }

  if (restoreState) {
    (void)savePersistentState();
  }
  return true;
}

void Nrf54MatterOnOffLightDevice::end() {
  if (!storageOpen_) {
    return;
  }
  prefs_.end();
  storageOpen_ = false;
}

void Nrf54MatterOnOffLightDevice::process() {
  // Handle identify timeout
  if (identifyEndMs_ != 0U &&
      remainingIdentifySeconds(identifyEndMs_) == 0U) {
    identifyEndMs_ = 0U;
    notifyIdentifyChange();
  }

  // Handle level transition
  if (levelTransitionEndMs_ != 0U) {
    const uint32_t nowMs = millis();
    if (nowMs >= levelTransitionEndMs_) {
      level_ = levelTarget_;
      levelTransitionEndMs_ = 0U;
    } else if (levelTransitionMs_ > 0U) {
      const uint32_t elapsed = nowMs - (levelTransitionEndMs_ - levelTransitionMs_);
      const int32_t diff = static_cast<int32_t>(levelTarget_) - static_cast<int32_t>(levelStart_);
      const int32_t progress = (diff * static_cast<int32_t>(elapsed)) /
                                static_cast<int32_t>(levelTransitionMs_);
      level_ = static_cast<uint8_t>(static_cast<int32_t>(levelStart_) + progress);
    }
  }
}

bool Nrf54MatterOnOffLightDevice::setOn(bool on, bool persist) {
  if (on_ == on) {
    return !persist || savePersistentState();
  }

  on_ = on;
  if (persist && !savePersistentState()) {
    return false;
  }
  notifyOnChange();
  return true;
}

bool Nrf54MatterOnOffLightDevice::toggle(bool persist) {
  return setOn(!on_, persist);
}

bool Nrf54MatterOnOffLightDevice::on() const {
  return on_;
}

bool Nrf54MatterOnOffLightDevice::setLevel(uint8_t level, bool persist) {
  if (level > kMaxLevel && level != 255U) {
    return false;
  }
  if (level > kMaxLevel) level = kMaxLevel;
  if (level < kMinLevel && level != 0U) level = kMinLevel;
  level_ = level;
  return !persist || savePersistentState();
}

uint8_t Nrf54MatterOnOffLightDevice::level() const {
  return level_;
}

bool Nrf54MatterOnOffLightDevice::moveToLevel(uint8_t targetLevel, uint16_t transitionTimeMs) {
  if (targetLevel > kMaxLevel && targetLevel != 255U) {
    return false;
  }
  if (targetLevel > kMaxLevel) targetLevel = kMaxLevel;
  if (targetLevel < kMinLevel && targetLevel != 0U) targetLevel = kMinLevel;
  levelStart_ = level_;
  levelTarget_ = targetLevel;
  levelTransitionMs_ = transitionTimeMs;
  levelTransitionEndMs_ = millis() + transitionTimeMs;
  return true;
}

bool Nrf54MatterOnOffLightDevice::readLevelAttribute(uint8_t* outLevel) const {
  if (outLevel == nullptr) return false;
  *outLevel = level_;
  return true;
}

bool Nrf54MatterOnOffLightDevice::readMinLevelAttribute(uint8_t* outLevel) const {
  if (outLevel == nullptr) return false;
  *outLevel = kMinLevel;
  return true;
}

bool Nrf54MatterOnOffLightDevice::readMaxLevelAttribute(uint8_t* outLevel) const {
  if (outLevel == nullptr) return false;
  *outLevel = kMaxLevel;
  return true;
}

bool Nrf54MatterOnOffLightDevice::setStartUpBehavior(
    MatterOnOffLightStartUpBehavior behavior, bool persist) {
  if (!startUpBehaviorValid(behavior)) {
    return false;
  }

  startUpBehavior_ = behavior;
  return !persist || savePersistentState();
}

MatterOnOffLightStartUpBehavior
Nrf54MatterOnOffLightDevice::startUpBehavior() const {
  return startUpBehavior_;
}

bool Nrf54MatterOnOffLightDevice::setIdentifyTimeSeconds(uint16_t seconds) {
  if (seconds == 0U) {
    stopIdentify();
    return true;
  }

  identifyEndMs_ = millis() + (static_cast<uint32_t>(seconds) * 1000UL);
  notifyIdentifyChange();
  return true;
}

void Nrf54MatterOnOffLightDevice::stopIdentify() {
  const bool wasActive = identifying();
  identifyEndMs_ = 0U;
  if (wasActive) {
    notifyIdentifyChange();
  }
}

bool Nrf54MatterOnOffLightDevice::identifying() const {
  return remainingIdentifySeconds(identifyEndMs_) != 0U;
}

uint16_t Nrf54MatterOnOffLightDevice::identifyTimeSeconds() const {
  return remainingIdentifySeconds(identifyEndMs_);
}

bool Nrf54MatterOnOffLightDevice::savePersistentState() {
  if (!storageOpen_) {
    return false;
  }

  MatterOnOffLightPersistentState state = {};
  state.magic = kPersistentStateMagic;
  state.version = kPersistentStateVersion;
  state.flags = on_ ? kPersistentFlagOn : 0U;
  state.startUpBehavior = static_cast<uint8_t>(startUpBehavior_);
  return prefs_.putBytes(kPersistentStateKey, &state, sizeof(state)) ==
         sizeof(state);
}

bool Nrf54MatterOnOffLightDevice::clearPersistentState() {
  return storageOpen_ && prefs_.remove(kPersistentStateKey);
}

bool Nrf54MatterOnOffLightDevice::snapshot(
    MatterOnOffLightDeviceState* outState) const {
  if (outState == nullptr) {
    return false;
  }

  outState->on = on_;
  outState->identifying = identifying();
  outState->persistentStorageOpen = storageOpen_;
  outState->identifyTimeSeconds = identifyTimeSeconds();
  outState->startUpBehavior = startUpBehavior_;
  outState->level = level_;
  return true;
}

bool Nrf54MatterOnOffLightDevice::readOnOffAttribute(bool* outOn) const {
  if (outOn == nullptr) {
    return false;
  }
  *outOn = on_;
  return true;
}

bool Nrf54MatterOnOffLightDevice::readGlobalSceneControlAttribute(
    bool* outEnabled) const {
  if (outEnabled == nullptr) {
    return false;
  }
  *outEnabled = globalSceneControl_;
  return true;
}

bool Nrf54MatterOnOffLightDevice::readIdentifyTimeAttribute(
    uint16_t* outSeconds) const {
  if (outSeconds == nullptr) {
    return false;
  }
  *outSeconds = identifyTimeSeconds();
  return true;
}

void Nrf54MatterOnOffLightDevice::setOnChangeCallback(OnChangeCallback callback,
                                                      void* context) {
  onChangeCallback_ = callback;
  onChangeContext_ = context;
}

void Nrf54MatterOnOffLightDevice::setIdentifyCallback(
    IdentifyCallback callback, void* context) {
  identifyCallback_ = callback;
  identifyContext_ = context;
}

bool Nrf54MatterOnOffLightDevice::persistentStateValid(
    const MatterOnOffLightPersistentState& state) {
  return state.magic == kPersistentStateMagic &&
         state.version == kPersistentStateVersion &&
         startUpBehaviorValid(static_cast<MatterOnOffLightStartUpBehavior>(
             state.startUpBehavior));
}

bool Nrf54MatterOnOffLightDevice::startUpBehaviorValid(
    MatterOnOffLightStartUpBehavior behavior) {
  switch (behavior) {
    case MatterOnOffLightStartUpBehavior::kForceOff:
    case MatterOnOffLightStartUpBehavior::kForceOn:
    case MatterOnOffLightStartUpBehavior::kTogglePrevious:
    case MatterOnOffLightStartUpBehavior::kRestorePrevious:
      return true;
    default:
      return false;
  }
}

const char* Nrf54MatterOnOffLightDevice::startUpBehaviorName(
    MatterOnOffLightStartUpBehavior behavior) {
  switch (behavior) {
    case MatterOnOffLightStartUpBehavior::kForceOff:
      return "force-off";
    case MatterOnOffLightStartUpBehavior::kForceOn:
      return "force-on";
    case MatterOnOffLightStartUpBehavior::kTogglePrevious:
      return "toggle-previous";
    case MatterOnOffLightStartUpBehavior::kRestorePrevious:
      return "restore-previous";
    default:
      return "unknown";
  }
}

bool Nrf54MatterOnOffLightDevice::loadPersistentState(
    MatterOnOffLightPersistentState* outState) {
  if (!storageOpen_ || outState == nullptr ||
      prefs_.getBytesLength(kPersistentStateKey) != sizeof(*outState)) {
    return false;
  }

  *outState = MatterOnOffLightPersistentState{};
  if (prefs_.getBytes(kPersistentStateKey, outState, sizeof(*outState)) !=
      sizeof(*outState)) {
    *outState = MatterOnOffLightPersistentState{};
    return false;
  }

  if (!persistentStateValid(*outState)) {
    *outState = MatterOnOffLightPersistentState{};
    return false;
  }
  return true;
}

bool Nrf54MatterOnOffLightDevice::applyStartUpBehavior(bool previousOn) {
  switch (startUpBehavior_) {
    case MatterOnOffLightStartUpBehavior::kForceOff:
      on_ = false;
      return true;
    case MatterOnOffLightStartUpBehavior::kForceOn:
      on_ = true;
      return true;
    case MatterOnOffLightStartUpBehavior::kTogglePrevious:
      on_ = !previousOn;
      return true;
    case MatterOnOffLightStartUpBehavior::kRestorePrevious:
      on_ = previousOn;
      return true;
    default:
      return false;
  }
}

void Nrf54MatterOnOffLightDevice::notifyOnChange() {
  if (onChangeCallback_ != nullptr) {
    onChangeCallback_(onChangeContext_, on_);
  }
}

void Nrf54MatterOnOffLightDevice::notifyIdentifyChange() {
  if (identifyCallback_ != nullptr) {
    identifyCallback_(identifyContext_, identifying(), identifyTimeSeconds());
  }
}

}  // namespace xiao_nrf54l15
#endif // NRF54L15_CLEAN_MATTER_CORE_ENABLE
