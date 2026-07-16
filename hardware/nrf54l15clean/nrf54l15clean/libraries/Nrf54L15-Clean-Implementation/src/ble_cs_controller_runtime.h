#pragma once

#include <stddef.h>
#include <stdint.h>

namespace xiao_nrf54l15 {

enum class BleCsControllerRole : uint8_t {
  kInitiator = 0U,
  kReflector = 1U,
};

struct BleCsControllerTestConfig {
  // The runtime currently accepts the hardware-qualified single-antenna
  // Mode 2/Submode 1/AA-only profile. Channel map, timing, nonce, and access
  // addresses remain configurable within that profile.
  uint32_t subeventLengthUs = 5000U;
  uint8_t mainModeType = 2U;
  uint8_t subModeType = 1U;
  uint8_t mode0Steps = 3U;
  uint8_t mainModeRepetition = 1U;
  uint8_t mainModeSteps = 8U;
  uint8_t rttType = 0U;
  uint8_t syncPhy = 1U;
  uint8_t syncAntenna = 1U;
  uint8_t transmitPower = 0x7FU;
  uint8_t tIp1Us = 145U;
  uint8_t tIp2Us = 145U;
  uint8_t tFcsUs = 150U;
  uint8_t tPmUs = 40U;
  uint16_t drbgNonce = 0x1234U;
  uint8_t channelMapRepetition = 1U;
  uint8_t channelMap[10] = {0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
                            0xFFU, 0xFFU, 0xFFU, 0xFFU, 0x07U};
  uint8_t channelSelectionType = 0U;
  uint8_t channelSelectionShape = 0U;
  uint8_t channelSelectionJump = 2U;
  uint32_t initiatorAccessAddress = 0x4D7B8A2FUL;
  uint32_t reflectorAccessAddress = 0x96F93DB1UL;
};

struct BleCsControllerHciPacket {
  static constexpr size_t kMaxDataLength = 258U;

  uint8_t messageType = 0U;
  uint16_t dataLength = 0U;
  uint8_t data[kMaxDataLength] = {};
};

class BleCsControllerRuntime {
 public:
  static constexpr int32_t kErrorInvalidState = -1000;
  static constexpr int32_t kErrorRequires128MHz = -1001;
  static constexpr int32_t kErrorRadioBusy = -1002;
  static constexpr int32_t kErrorControllerMemory = -1003;
  static constexpr int32_t kErrorEventOverflow = -1004;
  static constexpr int32_t kErrorUnsupportedTestConfig = -1005;

  BleCsControllerRuntime();
  ~BleCsControllerRuntime();

  BleCsControllerRuntime(const BleCsControllerRuntime&) = delete;
  BleCsControllerRuntime& operator=(const BleCsControllerRuntime&) = delete;

  bool begin();
  bool startTest(BleCsControllerRole role,
                 const BleCsControllerTestConfig& config = {});
  void poll();
  bool readPacket(BleCsControllerHciPacket* outPacket);
  bool stopTest();
  bool end();

  bool active() const;
  bool testRunning() const;
  bool testComplete() const;
  int32_t lastError() const;
  uint32_t droppedPacketCount() const;
  uint16_t controllerMemoryRequired() const;

 private:
  void resetPackets();
  void drainHciPackets();
  void recordPacket(uint8_t messageType, const uint8_t* data,
                    uint16_t dataLength);
  void setError(int32_t error);

  static constexpr uint8_t kPacketQueueDepth = 12U;
  BleCsControllerHciPacket packetQueue_[kPacketQueueDepth] = {};
  uint8_t packetHead_ = 0U;
  uint8_t packetTail_ = 0U;
  uint8_t packetCount_ = 0U;
  uint32_t radioOwnershipToken_ = 0U;
  bool active_ = false;
  bool testRunning_ = false;
  bool testComplete_ = false;
  int32_t lastError_ = 0;
  uint32_t droppedPacketCount_ = 0U;
  uint16_t controllerMemoryRequired_ = 0U;
};

}  // namespace xiao_nrf54l15
