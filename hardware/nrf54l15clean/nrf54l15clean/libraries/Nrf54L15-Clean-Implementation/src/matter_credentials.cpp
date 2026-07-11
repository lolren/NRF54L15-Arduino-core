#if defined(NRF54L15_CLEAN_MATTER_CORE_ENABLE) && NRF54L15_CLEAN_MATTER_CORE_ENABLE
#include "matter_credentials.h"

#include <string.h>
#include <stdio.h>

#include "matter_manual_pairing.h"

namespace xiao_nrf54l15 {
namespace {

void copyPersistentCredentialsMembers(MatterCredentialsState* destination,
                                      const MatterCredentialsState& source) {
  destination->magic = source.magic;
  destination->version = source.version;
  destination->reserved = source.reserved;
  destination->setupPinCode = source.setupPinCode;
  destination->discriminator = source.discriminator;
  destination->vendorId = source.vendorId;
  destination->productId = source.productId;
}

void resetPersistentCredentialsState(MatterCredentialsState* state) {
  if (state == nullptr) {
    return;
  }

  // This structure is persisted as a full object blob. Clear its complete
  // representation first so any ABI padding remains deterministic, then apply
  // the typed defaults so future nonzero member initializers are preserved.
  uint8_t* bytes = reinterpret_cast<uint8_t*>(state);
  for (size_t i = 0; i < sizeof(*state); ++i) {
    bytes[i] = 0U;
  }
  const MatterCredentialsState defaults{};
  copyPersistentCredentialsMembers(state, defaults);
}

}  // namespace

bool MatterCredentials::begin(const char* namespaceName) {
  if (storageOpen_ || namespaceName == nullptr) {
    return false;
  }

  if (!prefs_.begin(namespaceName, false)) {
    return false;
  }

  storageOpen_ = true;
  stateLoaded_ = false;
  resetPersistentCredentialsState(&state_);

  if (loadState()) {
    return true;
  }

  // First boot, erased flash, or an incompatible credentials layout should
  // still leave Matter examples usable with the documented default identity.
  setDefaults();
  return saveState();
}

void MatterCredentials::end() {
  if (storageOpen_) {
    prefs_.end();
    storageOpen_ = false;
  }
  stateLoaded_ = false;
  resetPersistentCredentialsState(&state_);
}

uint32_t MatterCredentials::getSetupPinCode() const {
  if (stateLoaded_ && state_.setupPinCode != 0U) {
    return state_.setupPinCode;
  }
  return kDefaultSetupPinCode;
}

uint16_t MatterCredentials::getDiscriminator() const {
  if (stateLoaded_ && state_.discriminator != 0U) {
    return state_.discriminator;
  }
  return kDefaultDiscriminator;
}

uint16_t MatterCredentials::getVendorId() const {
  if (stateLoaded_ && state_.vendorId != 0U) {
    return state_.vendorId;
  }
  return kDefaultVendorId;
}

uint16_t MatterCredentials::getProductId() const {
  if (stateLoaded_ && state_.productId != 0U) {
    return state_.productId;
  }
  return kDefaultProductId;
}

bool MatterCredentials::setSetupPinCode(uint32_t pinCode) {
  if (!matterSetupPinValid(pinCode)) {
    return false;
  }
  if (!storageOpen_) {
    return false;
  }

  state_.setupPinCode = pinCode;
  return saveState();
}

bool MatterCredentials::setDiscriminator(uint16_t discriminator) {
  if (!matterDiscriminatorValid(discriminator)) {
    return false;
  }
  if (!storageOpen_) {
    return false;
  }

  state_.discriminator = discriminator;
  return saveState();
}

bool MatterCredentials::setVendorId(uint16_t vendorId) {
  if (!storageOpen_) {
    return false;
  }

  state_.vendorId = vendorId;
  return saveState();
}

bool MatterCredentials::setProductId(uint16_t productId) {
  if (!storageOpen_) {
    return false;
  }

  state_.productId = productId;
  return saveState();
}

bool MatterCredentials::setAll(uint32_t pinCode, uint16_t discriminator,
                                uint16_t vendorId, uint16_t productId) {
  if (!matterSetupPinValid(pinCode)) {
    return false;
  }
  if (!matterDiscriminatorValid(discriminator)) {
    return false;
  }
  if (!storageOpen_) {
    return false;
  }

  state_.setupPinCode = pinCode;
  state_.discriminator = discriminator;
  state_.vendorId = vendorId;
  state_.productId = productId;
  return saveState();
}

bool MatterCredentials::getSetupPinCodeString(
    char* outBuffer, size_t outBufferSize) const {
  if (outBuffer == nullptr || outBufferSize == 0U) {
    return false;
  }

  const uint32_t pin = getSetupPinCode();
  int written = snprintf(outBuffer, outBufferSize, "%lu",
                         static_cast<unsigned long>(pin));
  return written > 0 && static_cast<size_t>(written) < outBufferSize;
}

bool MatterCredentials::restoreDefaults() {
  if (!storageOpen_) {
    return false;
  }

  setDefaults();
  return saveState();
}

bool MatterCredentials::isReady() const {
  return storageOpen_ && stateLoaded_;
}

bool MatterCredentials::loadFromStorage(Preferences& prefs,
                                         MatterCredentialsState* outState) {
  if (outState == nullptr) {
    return false;
  }

  if (prefs.getBytesLength(kCredentialsKey) != sizeof(MatterCredentialsState)) {
    return false;
  }

  MatterCredentialsState state = {};
  if (prefs.getBytes(kCredentialsKey, &state, sizeof(state)) !=
      sizeof(state)) {
    return false;
  }

  if (state.magic != kStateMagic || state.version != kStateVersion) {
    return false;
  }

  // Validate loaded values
  if (!matterSetupPinValid(state.setupPinCode)) {
    return false;
  }
  if (!matterDiscriminatorValid(state.discriminator)) {
    return false;
  }

  *outState = state;
  return true;
}

bool MatterCredentials::saveToStorage(Preferences& prefs,
                                       const MatterCredentialsState& state) {
  MatterCredentialsState canonicalState;
  resetPersistentCredentialsState(&canonicalState);
  copyPersistentCredentialsMembers(&canonicalState, state);
  return prefs.putBytes(kCredentialsKey, &canonicalState,
                        sizeof(canonicalState)) == sizeof(canonicalState);
}

// Private helpers

void MatterCredentials::setDefaults() {
  state_.setupPinCode = kDefaultSetupPinCode;
  state_.discriminator = kDefaultDiscriminator;
  state_.vendorId = kDefaultVendorId;
  state_.productId = kDefaultProductId;
}

bool MatterCredentials::loadState() {
  if (!storageOpen_) {
    return false;
  }

  MatterCredentialsState loaded = {};
  if (!loadFromStorage(prefs_, &loaded)) {
    return false;
  }

  state_ = loaded;
  stateLoaded_ = true;
  return true;
}

bool MatterCredentials::saveState() {
  if (!storageOpen_) {
    return false;
  }

  state_.magic = kStateMagic;
  state_.version = kStateVersion;
  state_.reserved = 0U;

  if (!saveToStorage(prefs_, state_)) {
    return false;
  }

  stateLoaded_ = true;
  return true;
}

}  // namespace xiao_nrf54l15
#endif // NRF54L15_CLEAN_MATTER_CORE_ENABLE
