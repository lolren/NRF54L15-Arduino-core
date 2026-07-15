#include <assert.h>
#include <stdint.h>
#include <string.h>

#include <inet/IPAddress.h>

using chip::Inet::IPAddress;
using chip::Inet::InterfaceId;

int main() {
  IPAddress address;
  InterfaceId interfaceId;
  assert(IPAddress::FromString("fd12:3456::abcd", address, interfaceId) ==
         CHIP_NO_ERROR);
  assert(!interfaceId.IsPresent());
  const uint8_t expected[16] = {
      0xFD, 0x12, 0x34, 0x56, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0xAB, 0xCD,
  };
  assert(memcmp(address.mAddr, expected, sizeof(expected)) == 0);

  char text[IPAddress::kMaxStringLength] = {0};
  assert(address.ToString(text) == text);
  IPAddress roundTrip;
  assert(IPAddress::FromString(text, roundTrip, interfaceId) == CHIP_NO_ERROR);
  assert(roundTrip == address);

  assert(IPAddress::FromString("fe80::1%7", address, interfaceId) ==
         CHIP_NO_ERROR);
  assert(interfaceId == InterfaceId(7U));
  assert(IPAddress::FromString("::", address, interfaceId) == CHIP_NO_ERROR);
  assert(address == IPAddress::Any);
  assert(IPAddress::FromString("1:2:3:4:5:6:7:8", address, interfaceId) ==
         CHIP_NO_ERROR);

  assert(IPAddress::FromString("1:2:3", address, interfaceId) ==
         CHIP_ERROR_INVALID_ARGUMENT);
  assert(IPAddress::FromString("1::2::3", address, interfaceId) ==
         CHIP_ERROR_INVALID_ARGUMENT);
  assert(IPAddress::FromString("gggg::1", address, interfaceId) ==
         CHIP_ERROR_INVALID_ARGUMENT);
  assert(IPAddress::FromString("::1%name", address, interfaceId) ==
         CHIP_ERROR_INVALID_ARGUMENT);

  const IPAddress multicast = IPAddress::MakeIPv6PrefixMulticast(
      5U, 64U, 0xFD11223344556677ULL, 0x88001234UL);
  const uint8_t expectedMulticast[16] = {
      0xFF, 0x35, 0x00, 0x40, 0xFD, 0x11, 0x22, 0x33,
      0x44, 0x55, 0x66, 0x77, 0x88, 0x00, 0x12, 0x34,
  };
  assert(multicast.IsMulticast());
  assert(multicast.IsIPv6Multicast());
  assert(memcmp(multicast.mAddr, expectedMulticast,
                sizeof(expectedMulticast)) == 0);
  return 0;
}
