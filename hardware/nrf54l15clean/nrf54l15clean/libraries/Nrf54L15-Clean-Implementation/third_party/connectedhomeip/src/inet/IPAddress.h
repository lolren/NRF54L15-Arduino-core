#pragma once
#include <stdint.h>
#include <string.h>
#include <system/SystemError.h>
namespace chip { namespace Inet {
class InterfaceId {
public:
    static constexpr size_t kMaxIfNameLength = 16;
    uint32_t mValue = 0;
    constexpr InterfaceId() : mValue(0) {}
    constexpr InterfaceId(uint32_t v) : mValue(v) {}
    bool operator==(const InterfaceId & o) const { return mValue == o.mValue; }
    bool operator!=(const InterfaceId & o) const { return mValue != o.mValue; }
    bool IsPresent() const { return mValue != 0; }
    CHIP_ERROR GetInterfaceName(char * buf, size_t bufSize) const {
        if (buf && bufSize > 0) buf[0] = 0;
        return CHIP_NO_ERROR;
    }
};
struct InterfaceIdNullType {
    constexpr InterfaceId operator()() const { return InterfaceId(0); }
};
constexpr InterfaceIdNullType InterfaceIdNull() { return InterfaceIdNullType(); }
class IPAddress {
public:
    static constexpr size_t kMaxStringLength = 45;
    uint8_t mAddr[16];
    constexpr IPAddress() : mAddr{} {}
    bool IsMulticast() const { return false; }
    bool IsIPv6Multicast() const { return false; }
    void ToString(char * buf) const { (void)buf; }
    static CHIP_ERROR FromString(const char * str, IPAddress & addr, InterfaceId & iface) {
        memset(addr.mAddr, 0, sizeof(addr.mAddr));
        return CHIP_NO_ERROR;
    }
    static IPAddress MakeIPv6PrefixMulticast(uint8_t, uint8_t, uint64_t, uint32_t) {
        return IPAddress();
    }
};
class InetInterface {
public:
    static InetInterface * GetPrimary() { return nullptr; }
};
}}
namespace chip {
using Inet::InterfaceIdNull;
using Inet::InterfaceIdNullType;
}
