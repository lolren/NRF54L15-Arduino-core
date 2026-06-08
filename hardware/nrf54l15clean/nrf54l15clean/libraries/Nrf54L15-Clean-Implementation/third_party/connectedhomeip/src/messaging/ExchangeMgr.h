#pragma once
#include <system/SystemLayer.h>
#include <transport/raw/PeerAddress.h>
#include <transport/raw/Tuple.h>
namespace chip { namespace Messaging {
class ExchangeContext;
class ExchangeDelegate;
class ExchangeMgr {
public:
    virtual ~ExchangeMgr() {}
    virtual CHIP_ERROR NewContext(const Transport::PeerAddress & peerAddr, ExchangeDelegate * delegate, ExchangeContext ** ec) = 0;
};
}}
