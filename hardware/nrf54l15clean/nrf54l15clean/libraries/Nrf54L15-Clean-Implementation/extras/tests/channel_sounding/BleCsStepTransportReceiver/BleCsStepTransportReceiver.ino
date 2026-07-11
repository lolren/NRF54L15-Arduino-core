#include <Arduino.h>

#include "ble_channel_sounding.h"
#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

namespace {

BleChannelSoundingRadio gRadio;
uint8_t gPayload[700] = {0};

bool payloadMatches(uint16_t transferId, uint16_t length) {
  if (length != sizeof(gPayload)) {
    return false;
  }
  for (size_t i = 0U; i < sizeof(gPayload); ++i) {
    const uint8_t expected =
        static_cast<uint8_t>(i ^ transferId ^ (transferId >> 8U));
    if (gPayload[i] != expected) {
      return false;
    }
  }
  return true;
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(500U);
  BoardControl::setBatterySenseEnabled(false);
  BoardControl::setImuMicEnabled(false);
  if (!BoardControl::enableRfPath(BoardAntennaPath::kCeramic)) {
    Serial.println(F("transport_receiver rf_path=FAIL"));
    while (true) {
    }
  }

  BleCsConfig config{};
  config.maxPayloadLength = 255U;
  config.enableRawDfeCapture = false;
  if (!gRadio.begin(config)) {
    Serial.println(F("transport_receiver radio=FAIL"));
    while (true) {
    }
  }
  Serial.println(F("transport_receiver ready=1"));
}

void loop() {
  uint16_t length = 0U;
  uint16_t transferId = 0U;
  BleCsStepTransferStats stats{};
  const bool received = gRadio.receivePeerStepData(
      gPayload, sizeof(gPayload), &length, &transferId, 6000U, &stats);
  const bool ok = received && payloadMatches(transferId, length);
  Serial.print(F("transport_receiver result="));
  Serial.print(ok ? F("PASS") : F("RETRY"));
  Serial.print(F(" id="));
  Serial.print(transferId);
  Serial.print(F(" bytes="));
  Serial.print(length);
  Serial.print(F(" frames="));
  Serial.print(stats.framesReceived);
  Serial.print(F(" acks="));
  Serial.print(stats.acknowledgements);
  Serial.print(F(" duplicates="));
  Serial.print(stats.duplicateFrames);
  Serial.print(F(" rejected="));
  Serial.println(stats.rejectedFrames);
}
