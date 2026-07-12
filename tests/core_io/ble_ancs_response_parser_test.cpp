#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "utility/ancs_response_parser.h"

using bluefruit_compat::AncsResponseParser;

static void test_fragmented_notification_attribute() {
  const uint8_t prefix[] = {0x00, 0x78, 0x56, 0x34, 0x12};
  const uint8_t response[] = {
      0x00, 0x78, 0x56, 0x34, 0x12, 0x03, 0x0b, 0x00,
      'h',  'e',  'l',  'l',  'o',  ' ',  'w',  'o',
      'r',  'l',  'd',
  };
  uint8_t output[16] = {};
  AncsResponseParser parser;
  parser.begin(prefix, sizeof(prefix), 0x03, output, sizeof(output));
  parser.feed(response, 2);
  parser.feed(response + 2, 5);
  parser.feed(response + 7, 3);
  parser.feed(response + 10, sizeof(response) - 10);
  assert(parser.complete);
  assert(!parser.failed);
  assert(parser.valueLength == 11);
  assert(parser.valueCopied == 11);
  assert(memcmp(output, "hello world", 11) == 0);
}

static void test_app_attribute_and_truncation() {
  const uint8_t prefix[] = {0x01, 'c', 'o', 'm', '.', 'x', 0x00};
  const uint8_t response[] = {
      0x01, 'c', 'o', 'm', '.', 'x', 0x00, 0x00, 0x06, 0x00,
      'A',  'p', 'p', 'N', 'a', 'm',
  };
  uint8_t output[3] = {};
  AncsResponseParser parser;
  parser.begin(prefix, sizeof(prefix), 0x00, output, sizeof(output));
  parser.feed(response, sizeof(response));
  assert(parser.complete);
  assert(!parser.failed);
  assert(parser.valueLength == 6);
  assert(parser.valueCopied == sizeof(output));
  assert(memcmp(output, "App", sizeof(output)) == 0);
}

static void test_empty_and_malformed_responses() {
  const uint8_t prefix[] = {0x00, 1, 2, 3, 4};
  const uint8_t empty[] = {0x00, 1, 2, 3, 4, 0x05, 0x00, 0x00};
  uint8_t output[4] = {0xff, 0xff, 0xff, 0xff};
  AncsResponseParser parser;
  parser.begin(prefix, sizeof(prefix), 0x05, output, sizeof(output));
  parser.feed(empty, sizeof(empty));
  assert(parser.complete);
  assert(!parser.failed);
  assert(parser.valueLength == 0);
  assert(parser.valueCopied == 0);
  assert(output[0] == 0);

  const uint8_t wrongUid[] = {0x00, 1, 9, 3, 4, 0x05, 0x00, 0x00};
  parser.begin(prefix, sizeof(prefix), 0x05, output, sizeof(output));
  parser.feed(wrongUid, sizeof(wrongUid));
  assert(parser.failed);
  assert(!parser.complete);

  const uint8_t wrongAttribute[] = {0x00, 1, 2, 3, 4, 0x06, 0x00, 0x00};
  parser.begin(prefix, sizeof(prefix), 0x05, output, sizeof(output));
  parser.feed(wrongAttribute, sizeof(wrongAttribute));
  assert(parser.failed);
  assert(!parser.complete);
}

static void test_maximum_length_does_not_wrap_stream_offset() {
  const uint8_t prefix[] = {0x00, 1, 2, 3, 4};
  const uint8_t header[] = {0x00, 1, 2, 3, 4, 0x07, 0xff, 0xff};
  uint8_t output = 0;
  AncsResponseParser parser;
  parser.begin(prefix, sizeof(prefix), 0x07, &output, sizeof(output));
  parser.feed(header, sizeof(header));

  uint8_t chunk[255];
  memset(chunk, 'x', sizeof(chunk));
  uint32_t remaining = 0xffffU;
  while (remaining > 0U) {
    const uint16_t length = static_cast<uint16_t>(
        remaining > sizeof(chunk) ? sizeof(chunk) : remaining);
    parser.feed(chunk, length);
    remaining -= length;
  }

  assert(parser.complete);
  assert(!parser.failed);
  assert(parser.valueLength == 0xffffU);
  assert(parser.valueCopied == 1U);
  assert(output == 'x');
}

int main() {
  test_fragmented_notification_attribute();
  test_app_attribute_and_truncation();
  test_empty_and_malformed_responses();
  test_maximum_length_does_not_wrap_stream_offset();
  return 0;
}
