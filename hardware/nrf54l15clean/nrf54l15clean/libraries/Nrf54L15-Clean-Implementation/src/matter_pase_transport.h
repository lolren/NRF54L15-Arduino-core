#pragma once

#include <stdint.h>

#include <openthread/ip6.h>

namespace xiao_nrf54l15 {

class MatterPaseTransport {
 public:
  using ReceiveCallback = void (*)(void* context,
                                   const uint8_t* payload,
                                   uint16_t length,
                                   const otIp6Address& source,
                                   uint16_t sourcePort);

  virtual bool sendUdp(const uint8_t* payload, uint16_t length,
                       const otIp6Address& destAddr,
                       uint16_t destPort) = 0;
  virtual bool setReceiveCallback(ReceiveCallback callback,
                                  void* context = nullptr) = 0;

 protected:
  ~MatterPaseTransport() = default;
};

}  // namespace xiao_nrf54l15
