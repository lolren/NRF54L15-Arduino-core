#include <Arduino.h>
#include <Wire.h>
#include <bluefruit.h>
#include "nrf54l15_hal.h"
#include "npm1300.h"

using namespace xiao_nrf54l15;

namespace {

constexpr char kServiceUuid[] = "5f24c001-7e2b-4b8c-ae59-2d7618b9d1a0";
constexpr char kCapabilitiesUuid[] = "5f24c002-7e2b-4b8c-ae59-2d7618b9d1a0";
constexpr char kControlUuid[] = "5f24c003-7e2b-4b8c-ae59-2d7618b9d1a0";
constexpr char kStatusUuid[] = "5f24c004-7e2b-4b8c-ae59-2d7618b9d1a0";
constexpr char kAudioUuid[] = "5f24c005-7e2b-4b8c-ae59-2d7618b9d1a0";
constexpr char kImuUuid[] = "5f24c006-7e2b-4b8c-ae59-2d7618b9d1a0";

constexpr uint8_t kProtocolVersion = 1U;
constexpr uint8_t kCodecImaAdpcm = 1U;
constexpr uint8_t kStreamAudio = 0x01U;
constexpr uint8_t kStreamImu = 0x02U;
constexpr uint16_t kAudioSampleRate = 16000U;
constexpr size_t kAudioFrameSamples = 320U;
constexpr size_t kAudioHeaderBytes = 14U;
constexpr size_t kAudioPayloadBytes = (kAudioFrameSamples - 1U + 1U) / 2U;
constexpr size_t kAudioPacketBytes = kAudioHeaderBytes + kAudioPayloadBytes;
constexpr size_t kImuPacketBytes = 20U;
constexpr uint16_t kImuRateHz = 10U;
constexpr uint32_t kImuPeriodMs = 1000U / kImuRateHz;
constexpr uint8_t kStartupMuteFrames = 2U;
constexpr uint32_t kWakePeriodMs = 10000U;
constexpr uint32_t kAdvertisingWindowMs = 2500U;
constexpr uint32_t kSystemOffSleepMs = kWakePeriodMs - kAdvertisingWindowMs;
constexpr uint32_t kConnectedIdleTimeoutMs = 15000U;

constexpr uint8_t kImuAddress = 0x6AU;
constexpr uint8_t kImuWhoAmI = 0x0FU;
constexpr uint8_t kImuCtrl1Xl = 0x10U;
constexpr uint8_t kImuCtrl2G = 0x11U;
constexpr uint8_t kImuCtrl3C = 0x12U;
constexpr uint8_t kImuStatus = 0x1EU;
constexpr uint8_t kImuOutGyro = 0x22U;
constexpr uint8_t kImuCsPin = 39U;

BLEService g_service(kServiceUuid);
BLECharacteristic g_capabilities(kCapabilitiesUuid);
BLECharacteristic g_control(kControlUuid);
BLECharacteristic g_status(kStatusUuid);
BLECharacteristic g_audio(kAudioUuid);
BLECharacteristic g_imu(kImuUuid);

Pdm g_pdm;
alignas(4) int16_t g_pdmBuffers[2][kAudioFrameSamples] = {};
alignas(4) int16_t g_audioWork[kAudioFrameSamples] = {};
uint8_t g_audioPacket[kAudioPacketBytes] = {};
uint8_t g_imuPacket[kImuPacketBytes] = {};

volatile bool g_connected = false;
volatile bool g_sleepRequested = false;
volatile uint8_t g_requestedStreams = 0U;
volatile uint16_t g_controlTransaction = 0U;
volatile uint16_t g_connectionHandle = 0U;
volatile uint32_t g_lastControlActivityMs = 0U;
bool g_sensorsReady = false;
bool g_imuReady = false;
uint8_t g_startupFramesRemaining = 0U;
uint16_t g_audioSequence = 0U;
uint16_t g_imuSequence = 0U;
uint32_t g_audioSampleCounter = 0U;
uint16_t g_audioDrops = 0U;
uint16_t g_imuDrops = 0U;
uint8_t g_lastError = 0U;
bool g_audioDiscontinuity = true;
int g_imaStepIndex = 0;
uint32_t g_lastImuMs = 0U;
uint32_t g_lastStatusMs = 0U;
uint32_t g_advertisingStartedMs = 0U;

constexpr int16_t kImaStepTable[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31,
    34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130,
    143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449,
    494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411,
    1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026,
    4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442,
    11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623,
    27086, 29794, 32767};
constexpr int8_t kImaIndexTable[16] = {
    -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};

void putLe16(uint8_t* out, uint16_t value) {
  out[0] = static_cast<uint8_t>(value);
  out[1] = static_cast<uint8_t>(value >> 8U);
}

void putLe32(uint8_t* out, uint32_t value) {
  out[0] = static_cast<uint8_t>(value);
  out[1] = static_cast<uint8_t>(value >> 8U);
  out[2] = static_cast<uint8_t>(value >> 16U);
  out[3] = static_cast<uint8_t>(value >> 24U);
}

int16_t readLe16(const uint8_t* in) {
  return static_cast<int16_t>(static_cast<uint16_t>(in[0]) |
                              (static_cast<uint16_t>(in[1]) << 8U));
}

uint8_t encodeImaNibble(int16_t sample, int32_t& predictor, int& stepIndex) {
  const int step = kImaStepTable[stepIndex];
  int difference = static_cast<int>(sample) - predictor;
  uint8_t nibble = 0U;
  if (difference < 0) {
    nibble = 8U;
    difference = -difference;
  }

  int delta = step >> 3;
  if (difference >= step) {
    nibble |= 4U;
    difference -= step;
    delta += step;
  }
  if (difference >= (step >> 1)) {
    nibble |= 2U;
    difference -= step >> 1;
    delta += step >> 1;
  }
  if (difference >= (step >> 2)) {
    nibble |= 1U;
    delta += step >> 2;
  }

  predictor += ((nibble & 8U) != 0U) ? -delta : delta;
  predictor = constrain(predictor, -32768, 32767);
  stepIndex += kImaIndexTable[nibble];
  stepIndex = constrain(stepIndex, 0, 88);
  return nibble;
}

bool writeImuRegister(uint8_t reg, uint8_t value) {
  Wire1.beginTransmission(kImuAddress);
  Wire1.write(reg);
  Wire1.write(value);
  return Wire1.endTransmission(true) == 0U;
}

bool readImuRegisters(uint8_t reg, uint8_t* out, size_t length) {
  Wire1.beginTransmission(kImuAddress);
  Wire1.write(reg);
  if (Wire1.endTransmission(false) != 0U) {
    return false;
  }
  if (Wire1.requestFrom(static_cast<int>(kImuAddress),
                        static_cast<int>(length), 1) !=
      static_cast<int>(length)) {
    return false;
  }
  for (size_t i = 0U; i < length; ++i) {
    if (Wire1.available() <= 0) {
      return false;
    }
    out[i] = static_cast<uint8_t>(Wire1.read());
  }
  return true;
}

bool initializeSensors() {
  pinMode(kImuCsPin, OUTPUT);
  digitalWrite(kImuCsPin, HIGH);
  delay(10);

  if (!npm1300_imu_mic_power_enable(true)) {
    g_lastError = 1U;
    return false;
  }
  delay(50);
  if (!npm1300_ldo1_is_enabled()) {
    g_lastError = 2U;
    return false;
  }

  Wire1.begin();
  Wire1.setClock(400000UL);
  uint8_t whoAmI = 0U;
  g_imuReady = readImuRegisters(kImuWhoAmI, &whoAmI, 1U) &&
               whoAmI == kImuAddress &&
               writeImuRegister(kImuCtrl3C, 0x44U) &&
               writeImuRegister(kImuCtrl1Xl, 0x40U) &&
               writeImuRegister(kImuCtrl2G, 0x40U);
  if (!g_imuReady) {
    g_lastError = 3U;
  }

  const Pin pdmClock{1U, 13U};
  const Pin pdmData{1U, 14U};
  if (!g_pdm.begin(pdmClock, pdmData, true, 25U,
                   PDM_RATIO_RATIO_Ratio80, PdmEdge::kLeftFalling)) {
    g_lastError = 4U;
    return false;
  }
  return true;
}

void shutdownSensors() {
  if (g_pdm.isStreaming()) {
    (void)g_pdm.stopStream();
  }
  g_pdm.end();
  if (g_imuReady) {
    (void)writeImuRegister(kImuCtrl1Xl, 0U);
    (void)writeImuRegister(kImuCtrl2G, 0U);
  }
  Wire1.end();
  (void)npm1300_imu_mic_power_enable(false);
  (void)npm1300_prepare_for_sleep();
  g_sensorsReady = false;
  g_imuReady = false;
}

void sendStatus() {
  uint8_t packet[12] = {};
  packet[0] = kProtocolVersion;
  packet[1] = static_cast<uint8_t>((g_connected ? 0x01U : 0U) |
                                   (g_pdm.isStreaming() ? 0x02U : 0U) |
                                   (g_imuReady ? 0x04U : 0U));
  putLe16(&packet[2], g_controlTransaction);
  putLe16(&packet[4], g_audioDrops);
  putLe16(&packet[6], g_imuDrops);
  packet[8] = g_requestedStreams;
  packet[9] = kCodecImaAdpcm;
  packet[10] = g_lastError;
  packet[11] = 0U;
  g_status.write(packet, sizeof(packet));
  if (g_status.notifyEnabled()) {
    (void)g_status.notify(packet, sizeof(packet));
  }
}

void encodeAndSendAudio(const int16_t* samples) {
  int64_t sum = 0;
  for (size_t i = 0U; i < kAudioFrameSamples; ++i) {
    sum += samples[i];
  }
  const int32_t dc = static_cast<int32_t>(sum / kAudioFrameSamples);
  for (size_t i = 0U; i < kAudioFrameSamples; ++i) {
    const int32_t centered =
        (static_cast<int32_t>(samples[i]) - dc) * 2;
    g_audioWork[i] = static_cast<int16_t>(constrain(centered, -32768, 32767));
  }

  memset(g_audioPacket, 0, sizeof(g_audioPacket));
  g_audioPacket[0] = kProtocolVersion;
  g_audioPacket[1] = g_audioDiscontinuity ? 0x01U : 0U;
  putLe16(&g_audioPacket[2], g_audioSequence);
  putLe32(&g_audioPacket[4], g_audioSampleCounter);
  putLe16(&g_audioPacket[8], static_cast<uint16_t>(kAudioFrameSamples));
  int32_t predictor = g_audioWork[0];
  putLe16(&g_audioPacket[10], static_cast<uint16_t>(predictor));
  g_audioPacket[12] = static_cast<uint8_t>(g_imaStepIndex);
  g_audioPacket[13] = 0U;

  int index = g_imaStepIndex;
  for (size_t i = 1U; i < kAudioFrameSamples; ++i) {
    const uint8_t nibble = encodeImaNibble(g_audioWork[i], predictor, index);
    const size_t nibbleIndex = i - 1U;
    const size_t byteIndex = kAudioHeaderBytes + nibbleIndex / 2U;
    if ((nibbleIndex & 1U) == 0U) {
      g_audioPacket[byteIndex] = nibble;
    } else {
      g_audioPacket[byteIndex] |= static_cast<uint8_t>(nibble << 4U);
    }
  }
  g_imaStepIndex = index;

  const bool sent = g_audio.notifyEnabled() &&
                    g_audio.notify(g_audioPacket, sizeof(g_audioPacket));
  if (!sent) {
    if (g_audioDrops != UINT16_MAX) {
      ++g_audioDrops;
    }
    g_audioDiscontinuity = true;
  } else {
    g_audioDiscontinuity = false;
  }
  ++g_audioSequence;
  g_audioSampleCounter += kAudioFrameSamples;
}

void serviceAudio() {
  const bool shouldStream = g_sensorsReady && g_connected &&
      ((g_requestedStreams & kStreamAudio) != 0U) && g_audio.notifyEnabled();
  if (!shouldStream) {
    if (g_pdm.isStreaming()) {
      (void)g_pdm.stopStream();
    }
    return;
  }

  if (!g_pdm.isStreaming()) {
    if (!g_pdm.startStream(g_pdmBuffers[0], kAudioFrameSamples)) {
      g_lastError = 5U;
      return;
    }
    g_startupFramesRemaining = kStartupMuteFrames;
    g_audioDiscontinuity = true;
    g_imaStepIndex = 0;
  }

  Pdm::StreamEvent event{};
  while (g_pdm.pollStream(&event)) {
    if (event.busError || event.overflow) {
      g_lastError = event.busError ? 6U : 7U;
      g_audioDiscontinuity = true;
      return;
    }
    if (!event.bufferRequested) {
      continue;
    }
    if (event.releasedBuffer == nullptr) {
      if (!g_pdm.queueStreamBuffer(g_pdmBuffers[1])) {
        g_lastError = 8U;
      }
      continue;
    }

    memcpy(g_audioWork, event.releasedBuffer, sizeof(g_audioWork));
    if (!g_pdm.queueStreamBuffer(event.releasedBuffer)) {
      g_lastError = 9U;
      (void)g_pdm.stopStream();
      return;
    }
    if (g_startupFramesRemaining > 0U) {
      --g_startupFramesRemaining;
      continue;
    }
    encodeAndSendAudio(g_audioWork);
  }
}

void serviceImu() {
  if (!g_imuReady || !g_connected ||
      (g_requestedStreams & kStreamImu) == 0U || !g_imu.notifyEnabled()) {
    return;
  }
  const uint32_t now = millis();
  if (static_cast<uint32_t>(now - g_lastImuMs) < kImuPeriodMs) {
    return;
  }
  g_lastImuMs = now;

  uint8_t status = 0U;
  uint8_t raw[12] = {};
  if (!readImuRegisters(kImuStatus, &status, 1U) ||
      (status & 0x03U) == 0U ||
      !readImuRegisters(kImuOutGyro, raw, sizeof(raw))) {
    return;
  }

  const int16_t gx = readLe16(&raw[0]);
  const int16_t gy = readLe16(&raw[2]);
  const int16_t gz = readLe16(&raw[4]);
  const int16_t ax = readLe16(&raw[6]);
  const int16_t ay = readLe16(&raw[8]);
  const int16_t az = readLe16(&raw[10]);
  g_imuPacket[0] = kProtocolVersion;
  g_imuPacket[1] = 0U;
  putLe16(&g_imuPacket[2], g_imuSequence++);
  putLe32(&g_imuPacket[4], g_audioSampleCounter);
  putLe16(&g_imuPacket[8], static_cast<uint16_t>(ax));
  putLe16(&g_imuPacket[10], static_cast<uint16_t>(ay));
  putLe16(&g_imuPacket[12], static_cast<uint16_t>(az));
  putLe16(&g_imuPacket[14], static_cast<uint16_t>(gx));
  putLe16(&g_imuPacket[16], static_cast<uint16_t>(gy));
  putLe16(&g_imuPacket[18], static_cast<uint16_t>(gz));
  if (!g_imu.notify(g_imuPacket, sizeof(g_imuPacket))) {
    if (g_imuDrops != UINT16_MAX) {
      ++g_imuDrops;
    }
  }
}

void controlWritten(uint16_t, BLECharacteristic*, uint8_t* data, uint16_t len) {
  if (data == nullptr || len < 4U || data[0] != kProtocolVersion) {
    g_lastError = 10U;
    return;
  }
  const uint8_t requested = static_cast<uint8_t>(
      data[1] & (kStreamAudio | kStreamImu));
  g_requestedStreams = 0U;
  if ((requested & kStreamAudio) != 0U && g_audio.notifyEnabled()) {
    g_requestedStreams |= kStreamAudio;
  }
  if ((requested & kStreamImu) != 0U && g_imu.notifyEnabled()) {
    g_requestedStreams |= kStreamImu;
  }
  g_controlTransaction = static_cast<uint16_t>(data[2]) |
                         (static_cast<uint16_t>(data[3]) << 8U);
  g_lastControlActivityMs = millis();
  sendStatus();
}

void connected(uint16_t connectionHandle) {
  g_connected = true;
  g_sleepRequested = false;
  g_connectionHandle = connectionHandle;
  g_lastControlActivityMs = millis();
  BLEConnection* connection = Bluefruit.Connection(connectionHandle);
  if (connection != nullptr) {
    (void)connection->requestPHY(BLE_GAP_PHY_2MBPS);
    (void)connection->requestDataLengthUpdate();
    (void)connection->requestMtuExchange(247U);
  }
  sendStatus();
}

void disconnected(uint16_t, uint8_t) {
  g_connected = false;
  g_requestedStreams = 0U;
  g_connectionHandle = 0U;
  g_sleepRequested = true;
}

void startAdvertising() {
  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addService(g_service);
  Bluefruit.Advertising.addName();
  Bluefruit.ScanResponse.addName();
  Bluefruit.Advertising.restartOnDisconnect(false);
  Bluefruit.Advertising.setInterval(32U, 160U);
  Bluefruit.Advertising.setFastTimeout(30U);
  Bluefruit.Advertising.start(0U);
  g_advertisingStartedMs = millis();
}

void setupGatt() {
  const uint8_t capabilities[16] = {
      'S', 'V', 'I', 'M', kProtocolVersion, kCodecImaAdpcm,
      static_cast<uint8_t>(kAudioSampleRate),
      static_cast<uint8_t>(kAudioSampleRate >> 8U),
      static_cast<uint8_t>(kAudioFrameSamples),
      static_cast<uint8_t>(kAudioFrameSamples >> 8U),
      static_cast<uint8_t>(kImuRateHz), 0U,
      static_cast<uint8_t>(kAudioPacketBytes),
      static_cast<uint8_t>(kAudioPacketBytes >> 8U),
      static_cast<uint8_t>(kImuPacketBytes), 0U};

  g_service.begin();

  g_capabilities.setProperties(CHR_PROPS_READ);
  g_capabilities.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
  g_capabilities.setFixedLen(sizeof(capabilities));
  g_capabilities.begin();
  g_capabilities.write(capabilities, sizeof(capabilities));

  g_control.setProperties(CHR_PROPS_WRITE);
  g_control.setPermission(SECMODE_NO_ACCESS, SECMODE_OPEN);
  g_control.setFixedLen(4U);
  g_control.setWriteCallback(controlWritten);
  g_control.begin();

  g_status.setProperties(CHR_PROPS_READ | CHR_PROPS_NOTIFY);
  g_status.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
  g_status.setFixedLen(12U);
  g_status.begin();

  g_audio.setProperties(CHR_PROPS_NOTIFY);
  g_audio.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
  g_audio.setFixedLen(kAudioPacketBytes);
  g_audio.begin();

  g_imu.setProperties(CHR_PROPS_NOTIFY);
  g_imu.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
  g_imu.setFixedLen(kImuPacketBytes);
  g_imu.begin();
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(250);
  Serial.println("VoiceImuPeripheral 1.0");

  (void)npm1300_imu_mic_power_enable(false);
  (void)npm1300_prepare_for_sleep();

  Bluefruit.configUuid128Count(6U);
  Bluefruit.configPrphBandwidth(BANDWIDTH_MAX);
  Bluefruit.begin();
  Bluefruit.setName("XIAO Sense Voice");
  Bluefruit.setTxPower(4);
  Bluefruit.Periph.setConnectCallback(connected);
  Bluefruit.Periph.setDisconnectCallback(disconnected);
  Bluefruit.Periph.setConnInterval(6U, 12U);
  setupGatt();
  sendStatus();
  startAdvertising();
  Serial.println("Advertising as XIAO Sense Voice");
}

void loop() {
  if (g_connected && g_requestedStreams != 0U && !g_sensorsReady) {
    g_sensorsReady = initializeSensors();
    Serial.println(g_sensorsReady ? "Sensors ready" : "Sensor initialization failed");
    sendStatus();
  } else if (g_sensorsReady && (!g_connected || g_requestedStreams == 0U)) {
    shutdownSensors();
    sendStatus();
  }

  serviceAudio();
  serviceImu();
  const uint32_t now = millis();
  if (g_connected && g_requestedStreams == 0U &&
      static_cast<uint32_t>(now - g_lastControlActivityMs) >=
          kConnectedIdleTimeoutMs) {
    g_lastControlActivityMs = now;
    Serial.println("Idle central; disconnecting for low power");
    (void)Bluefruit.disconnect(g_connectionHandle);
  }
  if (g_connected && static_cast<uint32_t>(now - g_lastStatusMs) >= 1000U) {
    g_lastStatusMs = now;
    sendStatus();
  }

  if (!g_connected &&
      (g_sleepRequested ||
       static_cast<uint32_t>(now - g_advertisingStartedMs) >=
           kAdvertisingWindowMs)) {
    shutdownSensors();
    (void)Bluefruit.Advertising.stop();
    Serial.println("No listener; entering timed SystemOFF");
    Serial.flush();
    BoardControl::enterLowestPowerState();
    delaySystemOffNoRetention(kSystemOffSleepMs);
  }
  delayMicroseconds(100U);
}
