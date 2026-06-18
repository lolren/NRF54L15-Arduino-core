#pragma once
#include <system/SystemLayer.h>
#include <system/SystemPacketBuffer.h>
#include <transport/raw/PeerAddress.h>
#include <transport/raw/Tuple.h>
namespace chip { namespace Messaging {
class ExchangeContext {
public:
    virtual ~ExchangeContext() {}
    virtual CHIP_ERROR SendMessage(uint32_t type, System::PacketBufferHandle && msg) = 0;
    virtual void Close() = 0;
};
}}
