#ifndef BLUEFRUIT_COMPAT_ANCS_RESPONSE_PARSER_H_
#define BLUEFRUIT_COMPAT_ANCS_RESPONSE_PARSER_H_

#include <stddef.h>
#include <stdint.h>

namespace bluefruit_compat {

struct AncsResponseParser {
  const uint8_t* expectedPrefix = nullptr;
  uint8_t* output = nullptr;
  uint16_t expectedPrefixLength = 0U;
  uint16_t outputCapacity = 0U;
  uint32_t streamOffset = 0U;
  uint16_t valueLength = 0U;
  uint16_t valueCopied = 0U;
  uint8_t requestedAttribute = 0U;
  uint8_t lengthLow = 0U;
  bool lengthKnown = false;
  bool active = false;
  bool complete = false;
  bool failed = false;

  void begin(const uint8_t* prefix, uint16_t prefixLength,
             uint8_t attribute, void* buffer, uint16_t bufferSize) {
    expectedPrefix = prefix;
    output = static_cast<uint8_t*>(buffer);
    expectedPrefixLength = prefixLength;
    outputCapacity = bufferSize;
    streamOffset = 0U;
    valueLength = 0U;
    valueCopied = 0U;
    requestedAttribute = attribute;
    lengthLow = 0U;
    lengthKnown = false;
    active = true;
    complete = false;
    failed = false;
    if (output != nullptr && outputCapacity > 0U) {
      output[0] = 0U;
    }
  }

  void cancel() {
    expectedPrefix = nullptr;
    output = nullptr;
    expectedPrefixLength = 0U;
    outputCapacity = 0U;
    active = false;
    complete = false;
    failed = false;
  }

  void feed(const uint8_t* data, uint16_t length) {
    if (!active || complete || failed || data == nullptr || length == 0U) {
      return;
    }

    const uint32_t attributeOffset = expectedPrefixLength;
    const uint32_t lengthLowOffset = attributeOffset + 1U;
    const uint32_t lengthHighOffset = attributeOffset + 2U;
    const uint32_t valueOffset = attributeOffset + 3U;

    for (uint16_t i = 0U; i < length && !complete && !failed; ++i) {
      const uint32_t offset = streamOffset++;
      const uint8_t byte = data[i];
      if (offset < expectedPrefixLength) {
        if (expectedPrefix == nullptr || byte != expectedPrefix[offset]) {
          failed = true;
        }
        continue;
      }
      if (offset == attributeOffset) {
        if (byte != requestedAttribute) {
          failed = true;
        }
        continue;
      }
      if (offset == lengthLowOffset) {
        lengthLow = byte;
        continue;
      }
      if (offset == lengthHighOffset) {
        valueLength = static_cast<uint16_t>(
            lengthLow | (static_cast<uint16_t>(byte) << 8U));
        lengthKnown = true;
        if (valueLength == 0U) {
          complete = true;
        }
        continue;
      }
      if (!lengthKnown || offset < valueOffset) {
        failed = true;
        continue;
      }

      const uint32_t valueIndex = offset - valueOffset;
      if (valueIndex >= valueLength) {
        failed = true;
        continue;
      }
      if (valueIndex < outputCapacity && output != nullptr) {
        output[valueIndex] = byte;
        valueCopied = static_cast<uint16_t>(valueIndex + 1U);
      }
      if ((valueIndex + 1U) == valueLength) {
        complete = true;
      }
    }
  }
};

}  // namespace bluefruit_compat

#endif  // BLUEFRUIT_COMPAT_ANCS_RESPONSE_PARSER_H_
