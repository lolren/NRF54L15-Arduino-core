#pragma once
namespace chip { namespace Inet {
class InetInterface {
public:
    static InetInterface * GetPrimary() { return nullptr; }
};
}}
