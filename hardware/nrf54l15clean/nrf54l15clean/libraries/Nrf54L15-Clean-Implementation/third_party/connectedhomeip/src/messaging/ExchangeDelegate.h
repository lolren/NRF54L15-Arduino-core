#pragma once
#include <system/SystemLayer.h>
#include <system/SystemPacketBuffer.h>
#include <transport/raw/PeerAddress.h>
namespace chip { namespace Messaging {
class ExchangeContext;
class ExchangeDelegate {
public:
    virtual ~ExchangeDelegate() {}
    virtual void OnMessageReceived(ExchangeContext * ec, System::PacketBufferHandle && msg) = 0;
    virtual void OnResponseTimeout(ExchangeContext * ec) = 0;
};
}}
