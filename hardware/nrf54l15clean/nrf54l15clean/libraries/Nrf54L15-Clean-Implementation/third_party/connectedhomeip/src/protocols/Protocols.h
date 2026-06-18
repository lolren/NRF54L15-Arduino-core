#pragma once
#include <stdint.h>
namespace chip { namespace Protocols {
    struct Id {
        uint32_t value = 0;
        constexpr Id() : value(0) {}
        constexpr Id(uint32_t v) : value(v) {}
        bool operator==(const Id & o) const { return value == o.value; }
        bool operator!=(const Id & o) const { return value != o.value; }
        constexpr uint16_t GetVendorId() const { return 0; }
    };
    inline constexpr Id NotSpecified() { return Id(); }
    template<typename T>
    struct MessageTypeTraits {
        static constexpr Id ProtocolId = Id();
    };
    static constexpr uint32_t kInvalidProtocolId = 0;
}}
