#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <system/SystemError.h>

namespace chip {
namespace Inet {

enum class IPAddressType : uint8_t {
    kUnknown = 0,
    kIPv4 = 1,
    kIPv6 = 2,
};

class InterfaceId {
public:
    static constexpr size_t kMaxIfNameLength = 16;

    constexpr InterfaceId() : mValue(0) {}
    explicit constexpr InterfaceId(uint32_t value) : mValue(value) {}

    static constexpr InterfaceId Null() { return InterfaceId(); }
    bool operator==(const InterfaceId & other) const { return mValue == other.mValue; }
    bool operator!=(const InterfaceId & other) const { return !(*this == other); }
    bool IsPresent() const { return mValue != 0U; }

    CHIP_ERROR GetInterfaceName(char * buffer, size_t bufferSize) const {
        if (buffer == nullptr || bufferSize == 0U) {
            return CHIP_ERROR_INVALID_ARGUMENT;
        }
        const int written = snprintf(buffer, bufferSize, "%lu",
                                     static_cast<unsigned long>(mValue));
        return written >= 0 && static_cast<size_t>(written) < bufferSize
            ? CHIP_NO_ERROR
            : CHIP_ERROR_BUFFER_TOO_SMALL;
    }

    uint32_t GetPlatformInterface() const { return mValue; }

private:
    uint32_t mValue;
};

struct InterfaceIdNullType {
    constexpr InterfaceId operator()() const { return InterfaceId::Null(); }
};

constexpr InterfaceIdNullType InterfaceIdNull() { return InterfaceIdNullType(); }

class IPAddress {
public:
    static constexpr size_t kMaxStringLength = 46U;

    constexpr IPAddress() : mAddr{} {}

    bool operator==(const IPAddress & other) const {
        return memcmp(mAddr, other.mAddr, sizeof(mAddr)) == 0;
    }
    bool operator!=(const IPAddress & other) const { return !(*this == other); }

    IPAddressType Type() const { return IPAddressType::kIPv6; }
    bool IsIPv4() const { return false; }
    bool IsIPv6() const { return true; }
    bool IsMulticast() const { return mAddr[0] == 0xFFU; }
    bool IsIPv6Multicast() const { return IsMulticast(); }

    char * ToString(char * buffer) const {
        if (buffer == nullptr) return nullptr;
        snprintf(buffer, kMaxStringLength,
                 "%x:%x:%x:%x:%x:%x:%x:%x",
                 wordAt(0U), wordAt(1U), wordAt(2U), wordAt(3U),
                 wordAt(4U), wordAt(5U), wordAt(6U), wordAt(7U));
        return buffer;
    }

    static CHIP_ERROR FromString(const char * text, IPAddress & address,
                                 InterfaceId & interfaceId) {
        address = IPAddress();
        interfaceId = InterfaceId::Null();
        if (text == nullptr || text[0] == '\0') {
            return CHIP_ERROR_INVALID_ARGUMENT;
        }

        const char * addressEnd = text + strlen(text);
        const char * zone = strchr(text, '%');
        if (zone != nullptr) {
            addressEnd = zone;
            if (zone[1] == '\0') return CHIP_ERROR_INVALID_ARGUMENT;
            uint32_t value = 0U;
            for (const char * cursor = zone + 1; *cursor != '\0'; ++cursor) {
                if (*cursor < '0' || *cursor > '9') {
                    return CHIP_ERROR_INVALID_ARGUMENT;
                }
                const uint32_t digit = static_cast<uint32_t>(*cursor - '0');
                if (value > ((UINT32_MAX - digit) / 10U)) {
                    return CHIP_ERROR_INVALID_ARGUMENT;
                }
                value = (value * 10U) + digit;
            }
            if (value == 0U) return CHIP_ERROR_INVALID_ARGUMENT;
            interfaceId = InterfaceId(value);
        }

        const char * compression = findDoubleColon(text, addressEnd);
        if (compression != nullptr &&
            findDoubleColon(compression + 2, addressEnd) != nullptr) {
            return CHIP_ERROR_INVALID_ARGUMENT;
        }

        uint16_t left[8] = {0};
        uint16_t right[8] = {0};
        size_t leftCount = 0U;
        size_t rightCount = 0U;

        if (compression == nullptr) {
            if (!parseWords(text, addressEnd, left, &leftCount) ||
                leftCount != 8U) {
                return CHIP_ERROR_INVALID_ARGUMENT;
            }
        } else {
            if (!parseWords(text, compression, left, &leftCount) ||
                !parseWords(compression + 2, addressEnd, right, &rightCount) ||
                leftCount + rightCount >= 8U) {
                return CHIP_ERROR_INVALID_ARGUMENT;
            }
        }

        size_t outputIndex = 0U;
        for (size_t i = 0U; i < leftCount; ++i) {
            storeWord(address.mAddr, outputIndex++, left[i]);
        }
        if (compression != nullptr) {
            outputIndex += 8U - leftCount - rightCount;
            for (size_t i = 0U; i < rightCount; ++i) {
                storeWord(address.mAddr, outputIndex++, right[i]);
            }
        }
        return CHIP_NO_ERROR;
    }

    static IPAddress MakeIPv6PrefixMulticast(uint8_t scope,
                                             uint8_t prefixLength,
                                             uint64_t prefix,
                                             uint32_t groupId) {
        IPAddress address;
        address.mAddr[0] = 0xFFU;
        address.mAddr[1] = static_cast<uint8_t>(0x30U | (scope & 0x0FU));
        address.mAddr[2] = 0U;
        address.mAddr[3] = prefixLength;
        for (size_t i = 0U; i < 8U; ++i) {
            address.mAddr[4U + i] = static_cast<uint8_t>(
                prefix >> ((7U - i) * 8U));
        }
        for (size_t i = 0U; i < 4U; ++i) {
            address.mAddr[12U + i] = static_cast<uint8_t>(
                groupId >> ((3U - i) * 8U));
        }
        return address;
    }

    static const IPAddress Any;
    uint8_t mAddr[16];

private:
    uint16_t wordAt(size_t index) const {
        const size_t offset = index * 2U;
        return static_cast<uint16_t>(
            (static_cast<uint16_t>(mAddr[offset]) << 8U) |
            static_cast<uint16_t>(mAddr[offset + 1U]));
    }

    static void storeWord(uint8_t output[16], size_t index, uint16_t word) {
        output[index * 2U] = static_cast<uint8_t>(word >> 8U);
        output[(index * 2U) + 1U] = static_cast<uint8_t>(word);
    }

    static int hexValue(char value) {
        if (value >= '0' && value <= '9') return value - '0';
        if (value >= 'a' && value <= 'f') return value - 'a' + 10;
        if (value >= 'A' && value <= 'F') return value - 'A' + 10;
        return -1;
    }

    static const char * findDoubleColon(const char * begin,
                                        const char * end) {
        if (begin == nullptr || end == nullptr || begin >= end) return nullptr;
        for (const char * cursor = begin; cursor + 1 < end; ++cursor) {
            if (cursor[0] == ':' && cursor[1] == ':') return cursor;
        }
        return nullptr;
    }

    static bool parseWords(const char * begin, const char * end,
                           uint16_t output[8], size_t * outputCount) {
        if (begin == nullptr || end == nullptr || output == nullptr ||
            outputCount == nullptr || begin > end) {
            return false;
        }
        *outputCount = 0U;
        if (begin == end) return true;

        const char * cursor = begin;
        while (cursor < end) {
            const char * tokenEnd = cursor;
            while (tokenEnd < end && *tokenEnd != ':') ++tokenEnd;
            const size_t tokenLength = static_cast<size_t>(tokenEnd - cursor);
            if (tokenLength == 0U || tokenLength > 4U || *outputCount >= 8U) {
                return false;
            }
            uint16_t word = 0U;
            for (const char * digit = cursor; digit < tokenEnd; ++digit) {
                const int value = hexValue(*digit);
                if (value < 0) return false;
                word = static_cast<uint16_t>((word << 4U) |
                                             static_cast<uint16_t>(value));
            }
            output[(*outputCount)++] = word;
            if (tokenEnd == end) break;
            cursor = tokenEnd + 1;
            if (cursor == end) return false;
        }
        return true;
    }
};

inline const IPAddress IPAddress::Any = IPAddress();

class InetInterface {
public:
    static InetInterface * GetPrimary() { return nullptr; }
};

}  // namespace Inet
}  // namespace chip

namespace chip {
using Inet::InterfaceIdNull;
using Inet::InterfaceIdNullType;
}  // namespace chip
