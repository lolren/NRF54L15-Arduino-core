#include <cassert>
#include <cstring>

#include "WString.h"

int main() {
  String self("0123456789");
  assert(self.concat(self));
  assert(std::strcmp(self.c_str(), "01234567890123456789") == 0);

  String substring("abcdef");
  const char* alias = substring.c_str() + 2;
  assert(substring.concat(alias));
  assert(std::strcmp(substring.c_str(), "abcdefcdef") == 0);

  String growth("x");
  for (unsigned i = 0; i < 14; ++i) {
    assert(growth.concat(growth));
  }
  assert(growth.length() == 16384U);
  return 0;
}
