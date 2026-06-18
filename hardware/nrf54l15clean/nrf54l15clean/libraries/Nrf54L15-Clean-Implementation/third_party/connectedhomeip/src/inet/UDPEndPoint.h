#pragma once
#include <system/SystemLayer.h>
#include <system/SystemPacketBuffer.h>
#include <system/SystemError.h>
#include <inet/IPAddress.h>
namespace chip { namespace Inet {
struct IPPacketInfo { IPAddress mAddress; uint16_t mPort; };
class UDPEndPoint {
public:
    virtual ~UDPEndPoint() {}
    virtual CHIP_ERROR BindUnspecifiedPort(const IPAddress&) { return CHIP_NO_ERROR; }
    virtual CHIP_ERROR SendTo(const IPAddress&, uint16_t, System::PacketBufferHandle&&) = 0;
    virtual void Close() = 0;
    using ReceiveCallbackFunct = void(*)(UDPEndPoint*, System::PacketBufferHandle&&, const IPPacketInfo&);
    virtual void SetReceiveCallback(ReceiveCallbackFunct, void*) {}
};
}}
