#include <Arduino.h>

#include "ble_channel_sounding.h"
#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

namespace {

BleChannelSoundingRadio gRadio;
uint8_t gPayload[700] = {0};
uint16_t gTransferId = 1U;

void fillPayload(uint16_t transferId) {
  for (size_t i = 0U; i < sizeof(gPayload); ++i) {
    gPayload[i] = static_cast<uint8_t>(i ^ transferId ^ (transferId >> 8U));
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(500U);
  BoardControl::setBatterySenseEnabled(false);
  BoardControl::setImuMicEnabled(false);
  if (!BoardControl::enableRfPath(BoardAntennaPath::kCeramic)) {
    Serial.println(F("transport_sender rf_path=FAIL"));
    while (true) {
    }
  }

  BleCsConfig config{};
  config.maxPayloadLength = 255U;
  config.enableRawDfeCapture = false;
  if (!gRadio.begin(config)) {
    Serial.println(F("transport_sender radio=FAIL"));
    while (true) {
    }
  }
  Serial.println(F("transport_sender ready=1"));
}

void loop() {
  fillPayload(gTransferId);
  BleCsStepTransferStats stats{};
  const bool ok = gRadio.sendPeerStepData(
      gPayload, sizeof(gPayload), gTransferId, 5000U, &stats);
  Serial.print(F("transport_sender result="));
  Serial.print(ok ? F("PASS") : F("RETRY"));
  Serial.print(F(" id="));
  Serial.print(gTransferId);
  Serial.print(F(" bytes="));
  Serial.print(stats.bytesTransferred);
  Serial.print(F(" frames="));
  Serial.print(stats.framesSent);
  Serial.print(F(" acks="));
  Serial.print(stats.acknowledgements);
  Serial.print(F(" retries="));
  Serial.println(stats.retries);
  if (ok) {
    ++gTransferId;
  }
  delay(100U);
}
