#pragma once
#include <system/SystemLayer.h>
namespace chip { namespace Inet {
class TCPEndPoint {
public:
    using ConnectCompleteFunct = void(*)(TCPEndPoint*, CHIP_ERROR);
    void SetConnectCompleteCallback(ConnectCompleteFunct, void*) {}
};
}}
