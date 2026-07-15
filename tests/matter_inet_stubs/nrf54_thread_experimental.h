#pragma once

#include <stddef.h>
#include <stdint.h>

struct otIp6Address {
  union {
    uint8_t m8[16];
  } mFields;
};

struct otMessageInfo {
  otIp6Address mPeerAddr = {};
  uint16_t mPeerPort = 0U;
};

namespace xiao_nrf54l15 {

class Nrf54ThreadExperimental {
 public:
  using UdpReceiveCallback = void (*)(void*, const uint8_t*, uint16_t,
                                      const otMessageInfo&);

  bool openUdp(uint16_t port, UdpReceiveCallback callback, void* context) {
    ++openCalls;
    lastPort = port;
    receiveCallback = callback;
    receiveContext = context;
    return openResult;
  }

  bool closeUdp(uint16_t port) {
    ++closeCalls;
    lastPort = port;
    return closeResult;
  }

  bool sendUdpFrom(uint16_t, const otIp6Address&, uint16_t, const void*,
                   uint16_t) {
    return sendResult;
  }

  void process() { ++processCalls; }

  bool openResult = true;
  bool closeResult = true;
  bool sendResult = true;
  unsigned openCalls = 0U;
  unsigned closeCalls = 0U;
  unsigned processCalls = 0U;
  uint16_t lastPort = 0U;
  UdpReceiveCallback receiveCallback = nullptr;
  void* receiveContext = nullptr;
};

}  // namespace xiao_nrf54l15
