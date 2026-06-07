#pragma once

#include <Preferences.h>
#include <stddef.h>
#include <stdint.h>

namespace xiao_nrf54l15 {

// Default credential values per Matter specification
constexpr uint32_t kDefaultSetupPinCode = 20202021UL;
constexpr uint16_t kDefaultDiscriminator = 3840U;
constexpr uint16_t kDefaultVendorId = 0xFFF1U;   // Matter test vendor ID.
constexpr uint16_t kDefaultProductId = 0x0001U;

// Storage keys for persistent credentials
constexpr char kCredentialsKey[] = "matter_creds";

// Persistent storage structure for credentials
struct MatterCredentialsState {
  uint32_t magic = 0U;
  uint16_t version = 0U;
  uint16_t reserved = 0U;
  uint32_t setupPinCode = 0U;
  uint16_t discriminator = 0U;
  uint16_t vendorId = 0U;
  uint16_t productId = 0U;
};

class MatterCredentials {
 public:
  static constexpr uint32_t kStateMagic = 0x43524544UL;  // "CRED"
  static constexpr uint16_t kStateVersion = 1U;

  MatterCredentials() = default;

  // Initialize storage with a namespace. Call once at startup.
  bool begin(const char* namespaceName);
  void end();

  // Getters return stored value or default.
  uint32_t getSetupPinCode() const;
  uint16_t getDiscriminator() const;
  uint16_t getVendorId() const;
  uint16_t getProductId() const;

  // Setters persist to storage.
  bool setSetupPinCode(uint32_t pinCode);
  bool setDiscriminator(uint16_t discriminator);
  bool setVendorId(uint16_t vendorId);
  bool setProductId(uint16_t productId);

  // Set all credentials at once
  bool setAll(uint32_t pinCode, uint16_t discriminator,
              uint16_t vendorId, uint16_t productId);

  // Get PIN as string for QR code / manual pairing display
  bool getSetupPinCodeString(char* outBuffer, size_t outBufferSize) const;

  // Restore factory defaults
  bool restoreDefaults();

  // Check if storage is initialized
  bool isReady() const;

  // Load from persistent storage (returns false if no valid stored data)
  static bool loadFromStorage(Preferences& prefs,
                              MatterCredentialsState* outState);
  // Save to persistent storage
  static bool saveToStorage(Preferences& prefs,
                            const MatterCredentialsState& state);

 private:
  void setDefaults();
  bool loadState();
  bool saveState();

  Preferences prefs_;
  bool storageOpen_ = false;
  MatterCredentialsState state_ = {};
  bool stateLoaded_ = false;
};

}  // namespace xiao_nrf54l15
