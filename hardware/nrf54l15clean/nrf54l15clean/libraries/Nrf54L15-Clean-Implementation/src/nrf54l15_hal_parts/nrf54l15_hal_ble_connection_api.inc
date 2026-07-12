bool BleRadio::initiateConnection(const uint8_t peerAddress[6],
                                  bool peerAddressRandom,
                                  uint16_t intervalUnits,
                                  uint16_t supervisionTimeoutUnits,
                                  uint8_t hopIncrement,
                                  uint32_t perChannelScanSpinLimit,
                                  BleAdvertisingChannel preferredChannel) {
  if (!initialized_ || radio_ == nullptr || connected_ || peerAddress == nullptr) {
    return false;
  }
  if (!connParamsAreValid(intervalUnits, intervalUnits, 0U,
                          supervisionTimeoutUnits) ||
      hopIncrement < 5U || hopIncrement > 16U) {
    return false;
  }
  if (!serviceResolvablePrivateAddressRotation()) {
    return false;
  }
  if (!beginUnconnectedRadioActivity(1500000UL)) {
    return false;
  }

  static constexpr BleAdvertisingChannel kChannels[3] = {
      BleAdvertisingChannel::k37,
      BleAdvertisingChannel::k38,
      BleAdvertisingChannel::k39,
  };
  static constexpr uint32_t kConnectDisableSpinPadding = 60000UL;
  static constexpr uint32_t kConnectFallbackMinListenSpin = 30000UL;

  const auto isPrimaryAdvertisingChannel = [](BleAdvertisingChannel channel) {
    return (channel == BleAdvertisingChannel::k37) ||
           (channel == BleAdvertisingChannel::k38) ||
           (channel == BleAdvertisingChannel::k39);
  };

  BleAdvertisingChannel channelOrder[3] = {
      BleAdvertisingChannel::k37,
      BleAdvertisingChannel::k38,
      BleAdvertisingChannel::k39,
  };
  uint8_t orderCount = 0U;
  const bool hasPreferredChannel = isPrimaryAdvertisingChannel(preferredChannel);
  if (hasPreferredChannel) {
    channelOrder[orderCount++] = preferredChannel;
  }
  for (uint8_t i = 0U; i < 3U; ++i) {
    const uint8_t idx = static_cast<uint8_t>((scanCycleStartIndex_ + i) % 3U);
    const BleAdvertisingChannel channel = kChannels[idx];
    if (hasPreferredChannel && (channel == preferredChannel)) {
      continue;
    }
    channelOrder[orderCount++] = channel;
  }

  for (uint8_t orderIdx = 0U; orderIdx < orderCount; ++orderIdx) {
    const BleAdvertisingChannel channel = channelOrder[orderIdx];
    uint32_t listenSpin = perChannelScanSpinLimit;
    if (!hasPreferredChannel || (orderIdx != 0U)) {
      listenSpin = (perChannelScanSpinLimit > 1U)
                       ? (perChannelScanSpinLimit / (hasPreferredChannel ? 3U : 2U))
                       : perChannelScanSpinLimit;
      if (listenSpin < kConnectFallbackMinListenSpin) {
        listenSpin = kConnectFallbackMinListenSpin;
      }
    }
    const uint32_t totalSpin = listenSpin + kConnectDisableSpinPadding;
    if (initiateConnectionOnce(channel, peerAddress, peerAddressRandom,
                               intervalUnits, supervisionTimeoutUnits,
                               hopIncrement, listenSpin, totalSpin)) {
      for (uint8_t idx = 0U; idx < 3U; ++idx) {
        if (kChannels[idx] == channel) {
          scanCycleStartIndex_ = static_cast<uint8_t>((idx + 1U) % 3U);
          break;
        }
      }
      return true;
    }
  }

  scanCycleStartIndex_ = static_cast<uint8_t>((scanCycleStartIndex_ + 1U) % 3U);
  endUnconnectedRadioActivity();
  return false;
}

bool BleRadio::initiateConnectionBudgeted(
    const uint8_t peerAddress[6], bool peerAddressRandom, uint16_t intervalUnits,
    uint16_t supervisionTimeoutUnits, uint8_t hopIncrement,
    uint32_t perChannelScanBudgetUs, BleAdvertisingChannel preferredChannel) {
  if (!initialized_ || radio_ == nullptr || connected_ || peerAddress == nullptr) {
    return false;
  }
  if (!connParamsAreValid(intervalUnits, intervalUnits, 0U,
                          supervisionTimeoutUnits) ||
      hopIncrement < 5U || hopIncrement > 16U) {
    return false;
  }
  if (!serviceResolvablePrivateAddressRotation()) {
    return false;
  }
  if (!beginUnconnectedRadioActivity(1500000UL)) {
    return false;
  }

  static constexpr BleAdvertisingChannel kChannels[3] = {
      BleAdvertisingChannel::k37,
      BleAdvertisingChannel::k38,
      BleAdvertisingChannel::k39,
  };
  static constexpr uint32_t kConnectFallbackMinListenBudgetUs = 5000UL;

  const auto isPrimaryAdvertisingChannel = [](BleAdvertisingChannel channel) {
    return (channel == BleAdvertisingChannel::k37) ||
           (channel == BleAdvertisingChannel::k38) ||
           (channel == BleAdvertisingChannel::k39);
  };

  if (perChannelScanBudgetUs == 0U) {
    perChannelScanBudgetUs = kConnectFallbackMinListenBudgetUs;
  }

  BleAdvertisingChannel channelOrder[3] = {
      BleAdvertisingChannel::k37,
      BleAdvertisingChannel::k38,
      BleAdvertisingChannel::k39,
  };
  uint8_t orderCount = 0U;
  const bool hasPreferredChannel = isPrimaryAdvertisingChannel(preferredChannel);
  if (hasPreferredChannel) {
    channelOrder[orderCount++] = preferredChannel;
  }
  for (uint8_t i = 0U; i < 3U; ++i) {
    const uint8_t idx = static_cast<uint8_t>((scanCycleStartIndex_ + i) % 3U);
    const BleAdvertisingChannel channel = kChannels[idx];
    if (hasPreferredChannel && (channel == preferredChannel)) {
      continue;
    }
    channelOrder[orderCount++] = channel;
  }

  for (uint8_t orderIdx = 0U; orderIdx < orderCount; ++orderIdx) {
    const BleAdvertisingChannel channel = channelOrder[orderIdx];
    uint32_t listenBudgetUs = perChannelScanBudgetUs;
    if (!hasPreferredChannel || (orderIdx != 0U)) {
      listenBudgetUs =
          (perChannelScanBudgetUs > 1U)
              ? (perChannelScanBudgetUs / (hasPreferredChannel ? 3U : 2U))
              : perChannelScanBudgetUs;
      if (listenBudgetUs < kConnectFallbackMinListenBudgetUs) {
        listenBudgetUs = kConnectFallbackMinListenBudgetUs;
      }
    }
    if (initiateConnectionOnceBudgeted(channel, peerAddress, peerAddressRandom,
                                       intervalUnits, supervisionTimeoutUnits,
                                       hopIncrement, listenBudgetUs)) {
      for (uint8_t idx = 0U; idx < 3U; ++idx) {
        if (kChannels[idx] == channel) {
          scanCycleStartIndex_ = static_cast<uint8_t>((idx + 1U) % 3U);
          break;
        }
      }
      return true;
    }
  }

  scanCycleStartIndex_ = static_cast<uint8_t>((scanCycleStartIndex_ + 1U) % 3U);
  endUnconnectedRadioActivity();
  return false;
}

bool BleRadio::queueAttRequest(const uint8_t* attPayload, uint8_t attPayloadLength) {
  if (!connected_ || attPayload == nullptr || attPayloadLength == 0U ||
      connectionPendingTxValid_) {
    return false;
  }
  if (attPayloadLength > currentAttMtu()) {
    return false;
  }

  const uint16_t totalLength =
      static_cast<uint16_t>(kBleL2capHeaderLen + attPayloadLength);
  if (totalLength > sizeof(connectionPendingTxPayload_) ||
      totalLength > currentTxDataPduPayloadLength()) {
    return false;
  }

  writeLe16(&connectionPendingTxPayload_[0], attPayloadLength);
  writeLe16(&connectionPendingTxPayload_[2], kBleL2capCidAtt);
  memcpy(&connectionPendingTxPayload_[4], attPayload, attPayloadLength);
  connectionPendingTxLlid_ = kBlePduDataStartOrComplete;
  connectionPendingTxLength_ = static_cast<uint8_t>(totalLength);
  connectionPendingTxValid_ = true;
  return true;
}

bool BleRadio::isChannelSoundingLlControlOpcode(uint8_t opcode) const {
  return opcode >= kBleLlCtrlCsReq && opcode <= kBleLlCtrlCsAbort;
}

bool BleRadio::isChannelSoundingLlControlPdu(const uint8_t* payload,
                                             uint8_t length) const {
  if (payload == nullptr || length < 2U ||
      !isChannelSoundingLlControlOpcode(payload[0])) {
    return false;
  }

  return static_cast<uint8_t>(payload[1] + 2U) == length;
}

bool BleRadio::queueChannelSoundingLlControlPdu(const uint8_t* payload,
                                                uint8_t length) {
  if (!connected_ || payload == nullptr || length == 0U ||
      length > currentTxDataPduPayloadLength() ||
      !isChannelSoundingLlControlPdu(payload, length)) {
    return false;
  }

  const uint32_t irqState = bleEnterCritical();
  if (channelSoundingLlControlTxCount_ >= kChannelSoundingLlControlQueueDepth) {
    ++channelSoundingLlControlDebug_.txDropCount;
    bleExitCritical(irqState);
    return false;
  }

  memcpy(channelSoundingLlControlTxPayloads_[channelSoundingLlControlTxTail_],
         payload, length);
  channelSoundingLlControlTxLengths_[channelSoundingLlControlTxTail_] = length;
  channelSoundingLlControlTxTail_ =
      static_cast<uint8_t>((channelSoundingLlControlTxTail_ + 1U) %
                           kChannelSoundingLlControlQueueDepth);
  ++channelSoundingLlControlTxCount_;
  ++channelSoundingLlControlDebug_.txQueuedCount;
  channelSoundingLlControlDebug_.lastTxOpcode = payload[0];
  channelSoundingLlControlDebug_.lastTxLength = length;
  bleExitCritical(irqState);
  return true;
}

bool BleRadio::dequeueChannelSoundingLlControlTx(uint8_t* outPayload,
                                                 uint8_t* outLength) {
  if (outPayload == nullptr || outLength == nullptr ||
      channelSoundingLlControlTxCount_ == 0U) {
    return false;
  }

  const uint8_t index = channelSoundingLlControlTxHead_;
  const uint8_t length = channelSoundingLlControlTxLengths_[index];
  if (length == 0U || length > kBleDataPduMaxPayload) {
    channelSoundingLlControlTxHead_ =
        static_cast<uint8_t>((channelSoundingLlControlTxHead_ + 1U) %
                             kChannelSoundingLlControlQueueDepth);
    --channelSoundingLlControlTxCount_;
    ++channelSoundingLlControlDebug_.txDropCount;
    return false;
  }

  memcpy(outPayload, channelSoundingLlControlTxPayloads_[index], length);
  *outLength = length;
  memset(channelSoundingLlControlTxPayloads_[index], 0,
         sizeof(channelSoundingLlControlTxPayloads_[index]));
  channelSoundingLlControlTxLengths_[index] = 0U;
  channelSoundingLlControlTxHead_ =
      static_cast<uint8_t>((channelSoundingLlControlTxHead_ + 1U) %
                           kChannelSoundingLlControlQueueDepth);
  --channelSoundingLlControlTxCount_;
  ++channelSoundingLlControlDebug_.txSentCount;
  channelSoundingLlControlDebug_.lastTxOpcode = outPayload[0];
  channelSoundingLlControlDebug_.lastTxLength = length;
  return true;
}

void BleRadio::rememberChannelSoundingLlControlRx(const uint8_t* payload,
                                                  uint8_t length) {
  if (!isChannelSoundingLlControlPdu(payload, length)) {
    ++channelSoundingLlControlDebug_.rxDropCount;
    return;
  }

  ++channelSoundingLlControlDebug_.rxCount;
  channelSoundingLlControlDebug_.lastRxOpcode = payload[0];
  channelSoundingLlControlDebug_.lastRxLength = length;
}

void BleRadio::getChannelSoundingLlControlDebug(
    BleChannelSoundingLlControlDebug* out) const {
  if (out == nullptr) {
    return;
  }

  *out = channelSoundingLlControlDebug_;
}

void BleRadio::clearChannelSoundingLlControlDebug() {
  memset(&channelSoundingLlControlDebug_, 0,
         sizeof(channelSoundingLlControlDebug_));
}

void BleRadio::clearChannelSoundingLlControlState() {
  memset(channelSoundingLlControlTxPayloads_, 0,
         sizeof(channelSoundingLlControlTxPayloads_));
  memset(channelSoundingLlControlTxLengths_, 0,
         sizeof(channelSoundingLlControlTxLengths_));
  channelSoundingLlControlTxHead_ = 0U;
  channelSoundingLlControlTxTail_ = 0U;
  channelSoundingLlControlTxCount_ = 0U;
  clearChannelSoundingLlControlDebug();
}

bool BleRadio::queueAttReadRequest(uint16_t handle) {
  if (handle == 0U) {
    return false;
  }
  uint8_t request[3] = {kAttOpReadReq, 0U, 0U};
  writeLe16(&request[1], handle);
  return queueAttRequest(request, sizeof(request));
}

bool BleRadio::queueAttWriteRequest(uint16_t handle, const uint8_t* value,
                                    uint8_t valueLength, bool withResponse) {
  if (handle == 0U || (valueLength > 0U && value == nullptr) ||
      (static_cast<uint16_t>(3U + valueLength) > sizeof(connectionPendingTxPayload_))) {
    return false;
  }
  if (static_cast<uint16_t>(3U + valueLength) > currentAttMtu()) {
    return false;
  }

  uint8_t request[255] = {0};
  request[0] = withResponse ? kAttOpWriteReq : kAttOpWriteCmd;
  writeLe16(&request[1], handle);
  if (valueLength > 0U) {
    memcpy(&request[3], value, valueLength);
  }
  return queueAttRequest(request, static_cast<uint8_t>(3U + valueLength));
}

bool BleRadio::queueAttSignedWriteCommand(uint16_t handle,
                                          const uint8_t* value,
                                          uint8_t valueLength) {
  static constexpr uint32_t kLocalCounterReservation = 16U;
  const uint16_t attLength = static_cast<uint16_t>(
      3U + static_cast<uint16_t>(valueLength) + 12U);
  if (!connected_ || connectionPendingTxValid_ || handle == 0U ||
      (valueLength > 0U && value == nullptr) ||
      connectionEncSessionValid_ || connectionEncRxEnabled_ ||
      connectionEncTxEnabled_ || !bondRecordValid_ ||
      !smpLocalCsrkValid_ || attLength > currentAttMtu() ||
      attLength > 255U || smpLocalSignCounter_ == 0xFFFFFFFFUL ||
      bleRunningInIsr()) {
    return false;
  }

  if (smpLocalSignCounter_ >= smpLocalSignCounterReservedUntil_) {
    const uint32_t remaining = 0xFFFFFFFFUL - smpLocalSignCounter_;
    const uint32_t reserveDelta =
        (remaining >= kLocalCounterReservation)
            ? kLocalCounterReservation
            : remaining;
    if (reserveDelta == 0U) {
      return false;
    }
    const uint32_t reservedUntil = smpLocalSignCounter_ + reserveDelta;
    bondRecord_.localSignCounter = reservedUntil;
    if (!persistActiveSigningCounters(true)) {
      emitBleTrace("ATT_SIGNED_WRITE_COUNTER_PERSIST_FAILED");
      return false;
    }
    smpLocalSignCounterReservedUntil_ = reservedUntil;
    emitBleTrace("ATT_SIGNED_WRITE_COUNTER_RESERVED");
  }

  uint8_t request[255] = {0};
  request[0] = kAttOpSignedWriteCmd;
  writeLe16(&request[1], handle);
  if (valueLength > 0U) {
    memcpy(&request[3], value, valueLength);
  }

  const uint16_t messageLength = static_cast<uint16_t>(3U + valueLength);
  const uint32_t signCounter = smpLocalSignCounter_;
  writeLe32(&request[messageLength], signCounter);
  if (!bleComputeDataSignature(smpLocalCsrk_, request, messageLength,
                               signCounter, &request[messageLength + 4U])) {
    return false;
  }

  // The containing reservation was made durable before this PDU became
  // eligible for transmission. A failed queue leaves only a harmless gap.
  smpLocalSignCounter_ = signCounter + 1U;
  return queueAttRequest(request, static_cast<uint8_t>(attLength));
}

uint32_t BleRadio::signedWriteReplayRejectCount() const {
  return signedWriteReplayRejectCount_;
}

bool BleRadio::computeAttDataSignature(const uint8_t csrk[16],
                                       const uint8_t* attMessage,
                                       uint8_t attMessageLength,
                                       uint32_t signCounter,
                                       uint8_t outMac[8]) {
  return bleComputeDataSignature(csrk, attMessage, attMessageLength,
                                 signCounter, outMac);
}

bool BleRadio::queueAttPrepareWriteRequest(uint16_t handle, uint16_t offset,
                                           const uint8_t* value,
                                           uint8_t valueLength) {
  if (handle == 0U || (valueLength > 0U && value == nullptr) ||
      (static_cast<uint16_t>(5U + valueLength) > sizeof(connectionPendingTxPayload_))) {
    return false;
  }
  if (static_cast<uint16_t>(5U + valueLength) > currentAttMtu()) {
    return false;
  }

  uint8_t request[255] = {0};
  request[0] = kAttOpPrepareWriteReq;
  writeLe16(&request[1], handle);
  writeLe16(&request[3], offset);
  if (valueLength > 0U) {
    memcpy(&request[5], value, valueLength);
  }
  return queueAttRequest(request, static_cast<uint8_t>(5U + valueLength));
}

bool BleRadio::queueAttExecuteWriteRequest(bool execute) {
  const uint8_t request[2] = {kAttOpExecuteWriteReq,
                              static_cast<uint8_t>(execute ? 0x01U : 0x00U)};
  return queueAttRequest(request, sizeof(request));
}

bool BleRadio::queueAttCccdWrite(uint16_t cccdHandle, bool notify,
                                 bool indicate, bool withResponse) {
  uint8_t value[2] = {0U, 0U};
  uint16_t cccdValue = 0U;
  if (notify) {
    cccdValue |= 0x0001U;
  }
  if (indicate) {
    cccdValue |= 0x0002U;
  }
  writeLe16(value, cccdValue);
  return queueAttWriteRequest(cccdHandle, value, sizeof(value), withResponse);
}

void BleRadio::setPreferredConnectionParameters(uint16_t intervalMinUnits,
                                                uint16_t intervalMaxUnits,
                                                uint16_t latency,
                                                uint16_t timeoutUnits) {
  if (intervalMinUnits < 6U) {
    intervalMinUnits = 6U;
  } else if (intervalMinUnits > 3200U) {
    intervalMinUnits = 3200U;
  }

  if (intervalMaxUnits == 0U) {
    intervalMaxUnits = intervalMinUnits;
  } else if (intervalMaxUnits < intervalMinUnits) {
    intervalMaxUnits = intervalMinUnits;
  } else if (intervalMaxUnits > 3200U) {
    intervalMaxUnits = 3200U;
  }

  if (latency > 499U) {
    latency = 499U;
  }

  if (timeoutUnits < 10U) {
    timeoutUnits = 10U;
  } else if (timeoutUnits > 3200U) {
    timeoutUnits = 3200U;
  }

  const uint32_t irqState = bleEnterCritical();
  gapPpcpIntervalMin_ = intervalMinUnits;
  gapPpcpIntervalMax_ = intervalMaxUnits;
  gapPpcpLatency_ = latency;
  gapPpcpTimeout_ = timeoutUnits;
  if (connected_ && (connectionRole_ == BleConnectionRole::kPeripheral)) {
    connectionConnParamUpdatePending_ = true;
  }
  bleExitCritical(irqState);
}

bool BleRadio::requestPreferredPhy(uint8_t txPhys, uint8_t rxPhys) {
  if (!setPreferredPhyOptions(txPhys, rxPhys)) {
    return false;
  }
  connectionPhyRequestPending_ = true;
  connectionPhyRequestInFlight_ = false;
  if (!connectionPendingTxValid_) {
    (void)queuePhyUpdateRequest();
  }
  return true;
}

bool BleRadio::requestPHY(uint8_t phy) {
  const uint8_t normalized = supportedBlePhys(phy);
  if ((normalized == kBlePhyNone) ||
      ((normalized & static_cast<uint8_t>(normalized - 1U)) != 0U)) {
    return false;
  }
  return requestPreferredPhy(normalized, normalized);
}

bool BleRadio::setPreferredPhyOptions(uint8_t txPhys, uint8_t rxPhys) {
  if (!connected_) {
    return false;
  }

  const uint8_t normalizedTx = supportedBlePhys(txPhys);
  const uint8_t normalizedRx = supportedBlePhys(rxPhys);
  const uint8_t symmetric = symmetricBlePhys(normalizedTx, normalizedRx);
  if (symmetric == kBlePhyNone) {
    return false;
  }
  if ((symmetric != normalizedTx) || (symmetric != normalizedRx)) {
    emitBleTrace("LL_PHY_SYM_ONLY");
  }
  connectionPreferredTxPhyMask_ = symmetric;
  connectionPreferredRxPhyMask_ = symmetric;
  return true;
}

bool BleRadio::requestDataLengthUpdate() {
  if (!connected_) {
    return false;
  }
  connectionDataLengthUpdatePending_ = true;
  connectionDataLengthUpdateInFlight_ = false;
  connectionDataLengthUpdateComplete_ = false;
  if (!connectionPendingTxValid_) {
    (void)queueCentralDataLengthRequest();
  }
  return true;
}

bool BleRadio::requestAttMtuExchange(uint16_t mtu) {
  if (!connected_ || mtu < kBleDefaultAttMtu) {
    return false;
  }
  connectionRequestedAttMtu_ = minU16(mtu, localPreferredAttMtu());
  connectionAttMtuExchangePending_ = true;
  connectionAttMtuExchangeInFlight_ = false;
  connectionAttMtuExchangeComplete_ = false;
  if (!connectionPendingTxValid_) {
    (void)queueCentralAttMtuRequest();
  }
  return true;
}

void BleRadio::setPeripheralPreferredDataLength(uint16_t payloadLength) {
  if (payloadLength < kBleDefaultDataPduMaxPayload) {
    payloadLength = kBleDefaultDataPduMaxPayload;
  } else if (payloadLength > kBleDataPduMaxPayload) {
    payloadLength = kBleDataPduMaxPayload;
  }
  peripheralPreferredDataPduPayloadLength_ = payloadLength;
}

void BleRadio::setCentralPreferredDataLength(uint16_t payloadLength) {
  if (payloadLength < kBleDefaultDataPduMaxPayload) {
    payloadLength = kBleDefaultDataPduMaxPayload;
  } else if (payloadLength > kBleDataPduMaxPayload) {
    payloadLength = kBleDataPduMaxPayload;
  }
  centralPreferredDataPduPayloadLength_ = payloadLength;
}

void BleRadio::setPeripheralPreferredAttMtu(uint16_t mtu) {
  if (mtu < kBleDefaultAttMtu) {
    mtu = kBleDefaultAttMtu;
  } else if (mtu > kBleServerPreferredMtu) {
    mtu = kBleServerPreferredMtu;
  }
  peripheralPreferredAttMtu_ = mtu;
  if (connected_ && (connectionRole_ == BleConnectionRole::kPeripheral) &&
      !connectionAttMtuExchangeComplete_) {
    connectionRequestedAttMtu_ = mtu;
  }
}

void BleRadio::setCentralPreferredAttMtu(uint16_t mtu) {
  if (mtu < kBleDefaultAttMtu) {
    mtu = kBleDefaultAttMtu;
  } else if (mtu > kBleServerPreferredMtu) {
    mtu = kBleServerPreferredMtu;
  }
  centralPreferredAttMtu_ = mtu;
  if (connected_ && (connectionRole_ == BleConnectionRole::kCentral) &&
      !connectionAttMtuExchangeComplete_) {
    connectionRequestedAttMtu_ = mtu;
  }
}

bool BleRadio::isConnected() const { return connected_; }

BleConnectionRole BleRadio::connectionRole() const {
  return connected_ ? connectionRole_ : BleConnectionRole::kNone;
}

uint8_t BleRadio::currentTxPhy() const {
  return connected_ ? connectionCurrentTxPhy_
                    : static_cast<uint8_t>(kBlePhy1M);
}

uint8_t BleRadio::currentRxPhy() const {
  return connected_ ? connectionCurrentRxPhy_
                    : static_cast<uint8_t>(kBlePhy1M);
}

uint8_t BleRadio::getPHY() const {
  const uint8_t txPhy = currentTxPhy();
  const uint8_t rxPhy = currentRxPhy();
  return (txPhy == rxPhy) ? txPhy : static_cast<uint8_t>(kBlePhyNone);
}

bool BleRadio::isConnectionEncrypted() const {
  const uint32_t irqState = bleEnterCritical();
  const bool encrypted = connected_ &&
                         connectionEncSessionValid_ &&
                         connectionEncRxEnabled_ &&
                         connectionEncTxEnabled_;
  if (encrypted && !activeBondUseNotedForConnection_ &&
      activeBondId_ < kBleMaxBonds && bondKeyPrimedForConnection_) {
    BleRadio* const self = const_cast<BleRadio*>(this);
    ++self->bondRuntimeUseCounter_;
    if (self->bondRuntimeUseCounter_ == 0U) {
      self->bondRuntimeUseCounter_ = 1U;
    }
    self->bondRuntimeLastUsed_[activeBondId_] =
        self->bondRuntimeUseCounter_;
    self->activeBondUseNotedForConnection_ = true;
  }
  bleExitCritical(irqState);
  return encrypted;
}

bool BleRadio::isConnectionAuthenticated() const {
  const uint32_t irqState = bleEnterCritical();
  const bool authenticated = connected_ &&
                             connectionEncSessionValid_ &&
                             connectionEncRxEnabled_ &&
                             connectionEncTxEnabled_ &&
                             connectionEncAuthenticated_;
  bleExitCritical(irqState);
  return authenticated;
}

bool BleRadio::getConnectionInfo(BleConnectionInfo* info) const {
  if (info == nullptr) {
    return false;
  }
  const uint32_t irqState = bleEnterCritical();
  if (!connected_) {
    bleExitCritical(irqState);
    return false;
  }

  memcpy(info->peerAddress, connectionPeerAddress_, sizeof(info->peerAddress));
  info->peerAddressRandom = connectionPeerAddressRandom_;
  info->accessAddress = connectionAccessAddress_;
  info->crcInit = connectionCrcInit_;
  info->intervalUnits = connectionIntervalUnits_;
  info->latency = connectionLatency_;
  info->supervisionTimeoutUnits = connectionTimeoutUnits_;
  memcpy(info->channelMap, connectionChannelMap_, sizeof(info->channelMap));
  info->channelCount = connectionChannelCount_;
  info->hopIncrement = connectionHop_;
  info->sleepClockAccuracy = connectionSca_;
  info->txPhy = connectionCurrentTxPhy_;
  info->rxPhy = connectionCurrentRxPhy_;
  bleExitCritical(irqState);
  return true;
}

bool BleRadio::getConnectionTimingSnapshot(
    BleConnectionTimingSnapshot* snapshot) const {
  if (snapshot == nullptr) {
    return false;
  }

  const uint32_t irqState = bleEnterCritical();
  if (!connected_) {
    bleExitCritical(irqState);
    *snapshot = BleConnectionTimingSnapshot{};
    return false;
  }

  const uint32_t nowUs = bleTimingUs();
  snapshot->connected = true;
  snapshot->role = connectionRole_;
  snapshot->nextEventCounter = connectionEventCounter_;
  snapshot->missedEventCount = connectionMissedEventCount_;
  snapshot->intervalUnits = connectionIntervalUnits_;
  snapshot->intervalUs = (connectionIntervalUnits_ > 0U)
                             ? (static_cast<uint32_t>(connectionIntervalUnits_) *
                                1250UL)
                             : 0U;
  snapshot->nowUs = nowUs;
  snapshot->nextEventUs = connectionNextEventUs_;
  snapshot->timeUntilNextEventUs =
      timeReachedUs(nowUs, connectionNextEventUs_)
          ? 0U
          : static_cast<uint32_t>(connectionNextEventUs_ - nowUs);
  snapshot->firstEventListenUs = connectionFirstEventListenUs_;
  snapshot->syncAttemptsRemaining = connectionSyncAttemptsRemaining_;
  snapshot->txPhy = connectionCurrentTxPhy_;
  snapshot->rxPhy = connectionCurrentRxPhy_;
  snapshot->channelCount = connectionChannelCount_;
  snapshot->hopIncrement = connectionHop_;
  snapshot->sleepClockAccuracy = connectionSca_;
  bleExitCritical(irqState);
  return true;
}

void BleRadio::getEncryptionDebugCounters(BleEncryptionDebugCounters* out) const {
  if (out == nullptr) {
    return;
  }
  const uint32_t irqState = bleEnterCritical();
  *out = encDebug_;
  bleExitCritical(irqState);
}

void BleRadio::clearEncryptionDebugCounters() {
  const uint32_t irqState = bleEnterCritical();
  memset(&encDebug_, 0, sizeof(encDebug_));
  bleExitCritical(irqState);
}

void BleRadio::captureSecureConnectionsDebugState(
    BleSecureConnectionsDebugState* out) const {
  if (out == nullptr) {
    return;
  }

  memset(out, 0, sizeof(*out));
  out->valid = 1U;
  out->active = smpSecureConnectionsActive_ ? 1U : 0U;
  out->localInitiator = smpLocalInitiator_ ? 1U : 0U;
  out->wireFormatBigEndian = smpSecureConnectionsWireFormatBigEndian_ ? 1U : 0U;
  out->peerPublicKeyValid = smpSecureConnectionsPeerPublicKeyValid_ ? 1U : 0U;
  out->publicKeySent = smpSecureConnectionsPublicKeySent_ ? 1U : 0U;
  out->confirmSent = smpSecureConnectionsConfirmSent_ ? 1U : 0U;
  out->randomSent = smpSecureConnectionsRandomSent_ ? 1U : 0U;
  out->dhKeyReady = smpSecureConnectionsDhKeyReady_ ? 1U : 0U;
  out->localConfirmReady = smpSecureConnectionsLocalConfirmReady_ ? 1U : 0U;
  out->checkValuesReady = smpSecureConnectionsCheckValuesReady_ ? 1U : 0U;
  out->dhKeyCheckSent = smpSecureConnectionsDhKeyCheckSent_ ? 1U : 0U;
  out->receivedDhKeyCheckValid =
      smpSecureConnectionsReceivedDhKeyCheckValid_ ? 1U : 0U;
  out->deferredPublicKey = smpSecureConnectionsDeferredPublicKey_ ? 1U : 0U;
  out->deferredConfirm = smpSecureConnectionsDeferredConfirm_ ? 1U : 0U;
  out->deferredRandom = smpSecureConnectionsDeferredRandom_ ? 1U : 0U;
  out->deferredDhKeyCheck =
      smpSecureConnectionsDeferredDhKeyCheck_ ? 1U : 0U;
  out->pendingTxValid = connectionPendingTxValid_ ? 1U : 0U;
  out->pairingState = smpPairingState_;
  memcpy(out->localRandom, smpLocalRandom_, sizeof(out->localRandom));
  memcpy(out->peerRandom, smpPeerRandom_, sizeof(out->peerRandom));
  memcpy(out->localConfirm, smpLocalConfirm_, sizeof(out->localConfirm));
  memcpy(out->peerConfirm, smpPeerConfirm_, sizeof(out->peerConfirm));
  memcpy(out->localDhKeyCheck, smpSecureConnectionsLocalDhKeyCheck_,
         sizeof(out->localDhKeyCheck));
  memcpy(out->peerDhKeyCheck, smpSecureConnectionsPeerDhKeyCheck_,
         sizeof(out->peerDhKeyCheck));
  memcpy(out->receivedDhKeyCheck, smpSecureConnectionsReceivedDhKeyCheck_,
         sizeof(out->receivedDhKeyCheck));
  memcpy(out->localPublicKeyX, &smpSecureConnectionsPublicKey_[1],
         sizeof(out->localPublicKeyX));
  memcpy(out->peerPublicKeyX, &smpSecureConnectionsPeerPublicKey_[1],
         sizeof(out->peerPublicKeyX));
  out->lastPublicKeyDecodeFailed =
      smpSecureConnectionsLastPublicKeyDecodeFailed_ ? 1U : 0U;
  out->lastPublicKeyDecodeLength =
      smpSecureConnectionsLastPublicKeyDecodeLength_;
  memcpy(out->lastPublicKeyWire, smpSecureConnectionsLastPublicKeyWire_,
         sizeof(out->lastPublicKeyWire));
  out->localKeypairTimeUs = smpSecureConnectionsLocalKeypairTimeUs_;
  out->dhKeyTimeUs = smpSecureConnectionsDhKeyTimeUs_;
  out->checkValuesTimeUs = smpSecureConnectionsCheckValuesTimeUs_;
  out->cooperateHookCount = smpSecureConnectionsCooperateHookCount_;
  out->backgroundServiceCount = smpSecureConnectionsBackgroundServiceCount_;
  out->oobEnabled = smpOobEnabled_ ? 1U : 0U;
  out->oobLocalDataValid = smpOobDataLocalValid_ ? 1U : 0U;
  out->oobRemoteDataValid = smpOobDataRemoteValid_ ? 1U : 0U;
  memcpy(out->oobLocalR, smpOobLocal_.r, sizeof(out->oobLocalR));
  memcpy(out->oobLocalC, smpOobLocal_.c, sizeof(out->oobLocalC));
  memcpy(out->oobRemoteR, smpOobRemote_.r, sizeof(out->oobRemoteR));
  memcpy(out->oobRemoteC, smpOobRemote_.c, sizeof(out->oobRemoteC));
}

void BleRadio::latchSecureConnectionsDebugState() {
  captureSecureConnectionsDebugState(&lastSecureConnectionsDebugState_);
}

void BleRadio::getSecureConnectionsDebugState(
    BleSecureConnectionsDebugState* out) const {
  if (out == nullptr) {
    return;
  }

  const uint32_t irqState = bleEnterCritical();
  BleSecureConnectionsDebugState current{};
  captureSecureConnectionsDebugState(&current);
  const bool useCurrent =
      smpSecureConnectionsPeerPublicKeyValid_ ||
      smpSecureConnectionsPublicKeySent_ ||
      smpSecureConnectionsLocalConfirmReady_ ||
      smpSecureConnectionsConfirmSent_ ||
      smpSecureConnectionsRandomSent_ ||
      smpSecureConnectionsLastPublicKeyDecodeFailed_ ||
      (smpPairingState_ != 0U);
  *out = useCurrent ? current : lastSecureConnectionsDebugState_;
  bleExitCritical(irqState);
}

bool BleRadio::getDisconnectDebug(BleDisconnectDebug* out) const {
  if (out == nullptr || disconnectDebug_.valid == 0U) {
    return false;
  }
  *out = disconnectDebug_;
  return true;
}

void BleRadio::clearDisconnectDebug() { resetDisconnectDebugState(); }

bool BleRadio::hasBondRecord() const {
  if (bondLoadCallback_ != nullptr || bondSaveCallback_ != nullptr) {
    return bondRecordValid_;
  }
  return activeBleBondDbCount() != 0U;
}

bool BleRadio::getBondRecord(BleBondRecord* outRecord) const {
  if (outRecord == nullptr) {
    return false;
  }
  const uint32_t irqState = bleEnterCritical();
  if (!bondRecordValid_) {
    bleExitCritical(irqState);
    return false;
  }
  memcpy(outRecord, &bondRecord_, sizeof(*outRecord));
  bleExitCritical(irqState);
  return true;
}

bool BleRadio::getBondRecord(uint8_t bondId, BleBondRecord* outRecord,
                             BleConnectionRole role) const {
  if (outRecord == nullptr || bondId >= kBleMaxBonds) {
    return false;
  }
  if (bondLoadCallback_ != nullptr || bondSaveCallback_ != nullptr) {
    if (bondId != 0U || !bondRecordValid_) {
      return false;
    }
    memcpy(outRecord, &bondRecord_, sizeof(*outRecord));
    return true;
  }
  return loadBondProjection(bondId, role, outRecord);
}

uint8_t BleRadio::bondCount() const {
  if (bondLoadCallback_ != nullptr || bondSaveCallback_ != nullptr) {
    return bondRecordValid_ ? 1U : 0U;
  }
  return activeBleBondDbCount();
}

bool BleRadio::getBondInfo(uint8_t bondId, BleBondInfo* outInfo) const {
  if (outInfo == nullptr || bondId >= kBleMaxBonds) {
    return false;
  }
  if (bondLoadCallback_ != nullptr || bondSaveCallback_ != nullptr) {
    if (bondId != 0U || !bondRecordValid_) {
      return false;
    }
    BleBondDbPayload payload{};
    bleBondDbUpdateCoreFromRecord(
        bondRecord_, connectionRole_, &payload.core, false);
    bleBondDbPayloadToInfo(0U, payload, outInfo);
    return true;
  }
  BleBondDbPayload payload{};
  if (!readActiveBleBondDbPayload(bondId, &payload)) {
    return false;
  }
  bleBondDbPayloadToInfo(bondId, payload, outInfo);
  return true;
}

uint8_t BleRadio::enumerateBonds(BleBondInfo* outBonds,
                                 uint8_t capacity) const {
  const uint8_t count = bondCount();
  if (outBonds == nullptr || capacity == 0U) {
    return count;
  }
  BleBondInfo available[kBleMaxBonds]{};
  uint8_t availableCount = 0U;
  for (uint8_t bondId = 0U; bondId < kBleMaxBonds; ++bondId) {
    if (getBondInfo(bondId, &available[availableCount])) {
      ++availableCount;
    }
  }
  for (uint8_t i = 0U; i < availableCount; ++i) {
    uint8_t newest = i;
    for (uint8_t j = static_cast<uint8_t>(i + 1U); j < availableCount; ++j) {
      if (bleBondDbGenerationNewer(available[j].lastUsedGeneration,
                                   available[newest].lastUsedGeneration)) {
        newest = j;
      }
    }
    if (newest != i) {
      const BleBondInfo swap = available[i];
      available[i] = available[newest];
      available[newest] = swap;
    }
  }
  const uint8_t copied = capacity < availableCount ? capacity : availableCount;
  memcpy(outBonds, available, static_cast<size_t>(copied) * sizeof(*outBonds));
  return copied;
}

bool BleRadio::deleteBond(uint8_t bondId) {
  if (bleRunningInIsr() || bondId >= kBleMaxBonds ||
      (connected_ && activeBondId_ == bondId)) {
    return false;
  }
  if (bondLoadCallback_ != nullptr || bondSaveCallback_ != nullptr) {
    return bondId == 0U && clearBondRecord(true);
  }
  if (!readActiveBleBondDbPayload(bondId, nullptr) ||
      !eraseBleBondDbRecord(bondId)) {
    return false;
  }
  if (directedTargetBondId_ == bondId) {
    directedTargetBondId_ = kBleInvalidBondId;
    directedTargetBondGeneration_ = 0U;
    directedTargetPeerIrkValid_ = false;
    memset(directedTargetPeerIrk_, 0, sizeof(directedTargetPeerIrk_));
  }
  bondRuntimeLastUsed_[bondId] = 0U;
  if (activeBondId_ == bondId) {
    activeBondId_ = kBleInvalidBondId;
    activeBondGeneration_ = 0U;
    memset(&bondRecord_, 0, sizeof(bondRecord_));
    bondRecordValid_ = false;
  }
  emitBleTrace("BOND_DELETED");
  return true;
}

uint8_t BleRadio::activeBondId() const {
  return bondRecordValid_ ? activeBondId_ : kBleInvalidBondId;
}

bool BleRadio::clearBondRecord(bool clearPersistentStorage) {
  if (bleRunningInIsr()) {
    return false;
  }
  memset(&bondRecord_, 0, sizeof(bondRecord_));
  bondRecordValid_ = false;
  bondStorageLoaded_ = true;
  activeBondId_ = kBleInvalidBondId;
  activeBondGeneration_ = 0U;
  bondRuntimeUseCounter_ = 0U;
  memset(bondRuntimeLastUsed_, 0, sizeof(bondRuntimeLastUsed_));
  bondKeyPrimedForConnection_ = false;
  bondIdentityMatchedForConnection_ = false;
  activeBondMeetsSecurityPolicy_ = false;
  bondSigningCounterPersistPending_ = false;
  resetBondedServiceChangedState();
  if (clearPersistentStorage) {
    return clearPersistentBondRecord();
  }
  return true;
}

void BleRadio::setBondPersistenceCallbacks(BleBondLoadCallback loadCallback,
                                           BleBondSaveCallback saveCallback,
                                           BleBondClearCallback clearCallback,
                                           void* context) {
  bondLoadCallback_ = loadCallback;
  bondSaveCallback_ = saveCallback;
  bondClearCallback_ = clearCallback;
  bondCallbackContext_ = context;
  bondStorageLoaded_ = false;
  loadBondRecordFromPersistence();
}

void BleRadio::setTraceCallback(BleTraceCallback callback, void* context) {
  const uint32_t irqState = bleEnterCritical();
  traceCallback_ = callback;
  traceCallbackContext_ = context;
  bleExitCritical(irqState);
}

bool BleRadio::getLastDisconnectReason(uint8_t* outReason, bool* outRemote) const {
  if (outReason == nullptr) {
    return false;
  }
  const uint32_t irqState = bleEnterCritical();
  if (!lastDisconnectReasonValid_) {
    bleExitCritical(irqState);
    return false;
  }
  *outReason = lastDisconnectReason_;
  if (outRemote != nullptr) {
    *outRemote = lastDisconnectReasonRemote_;
  }
  bleExitCritical(irqState);
  return true;
}

bool BleRadio::getLatestConnectionRssiDbm(int8_t* outRssiDbm) const {
  if (outRssiDbm == nullptr || !connected_ || !latestConnectionRssiValid_) {
    return false;
  }
  *outRssiDbm = latestConnectionRssiDbm_;
  return true;
}

void BleRadio::rememberDisconnectReason(uint8_t reason, bool remote) {
  lastDisconnectReason_ = reason;
  lastDisconnectReasonValid_ = true;
  lastDisconnectReasonRemote_ = remote;
  encDebug_.connLastDisconnectReason = reason;
  encDebug_.connLastDisconnectRemote = remote ? 1U : 0U;
  encDebug_.connLastDisconnectValid = 1U;
  if (remote) {
    ++encDebug_.connDisconnectRemoteCount;
  } else {
    ++encDebug_.connDisconnectLocalCount;
  }
}

void BleRadio::resetConnectionEventStruct(BleConnectionEvent* event) {
  if (event == nullptr) {
    return;
  }

  memset(event, 0, sizeof(*event));
  event->txLlid = 0x01U;
}

void BleRadio::resetDisconnectDebugState() {
  memset(&disconnectDebug_, 0, sizeof(disconnectDebug_));
  lastObservedRxLlid_ = 0U;
  lastObservedRxLength_ = 0U;
  lastObservedRxOpcode_ = 0U;
  lastObservedRxNesn_ = 0U;
  lastObservedRxSn_ = 0U;
  lastObservedPacketIsNew_ = false;
  lastObservedPeerAckedLastTx_ = false;
}

void BleRadio::rememberConnectionRxDebug(uint8_t llid, uint8_t rxLength,
                                         uint8_t protocolOpcode, uint8_t nesn,
                                         uint8_t sn, bool packetIsNew,
                                         bool peerAckedLastTx) {
  lastObservedRxLlid_ = llid;
  lastObservedRxLength_ = rxLength;
  lastObservedRxOpcode_ = protocolOpcode;
  lastObservedRxNesn_ = nesn;
  lastObservedRxSn_ = sn;
  lastObservedPacketIsNew_ = packetIsNew;
  lastObservedPeerAckedLastTx_ = peerAckedLastTx;
}

void BleRadio::recordDisconnectDebug(BleDisconnectReason reason, uint8_t errorCode) {
  const uint32_t nextSequence = disconnectDebug_.sequence + 1U;
  memset(&disconnectDebug_, 0, sizeof(disconnectDebug_));
  disconnectDebug_.sequence = nextSequence;
  disconnectDebug_.nextEventUs = connectionNextEventUs_;
  disconnectDebug_.eventCounter = connectionEventCounter_;
  disconnectDebug_.missedEventCount = connectionMissedEventCount_;
  disconnectDebug_.valid = 1U;
  disconnectDebug_.reason = static_cast<uint8_t>(reason);
  disconnectDebug_.role = static_cast<uint8_t>(connectionRole_);
  disconnectDebug_.errorCode = errorCode;
  disconnectDebug_.expectedRxSn = connectionExpectedRxSn_;
  disconnectDebug_.txSn = connectionTxSn_;
  disconnectDebug_.freshTxAllowed = connectionFreshTxAllowed_ ? 1U : 0U;
  disconnectDebug_.txHistoryValid = connectionTxHistoryValid_ ? 1U : 0U;
  disconnectDebug_.pendingTxValid = connectionPendingTxValid_ ? 1U : 0U;
  disconnectDebug_.pendingTxLlid = connectionPendingTxLlid_;
  disconnectDebug_.pendingTxLength = connectionPendingTxLength_;
  disconnectDebug_.pendingTxOpcode = bleConnectionPayloadProtocolOpcode(
      connectionPendingTxLlid_, &connectionPendingTxPayload_[0],
      connectionPendingTxLength_);
  disconnectDebug_.lastTxLlid = connectionLastTxPlainLlid_;
  disconnectDebug_.lastTxLength = connectionLastTxPlainLength_;
  disconnectDebug_.lastTxOpcode = bleConnectionPayloadProtocolOpcode(
      connectionLastTxPlainLlid_, &connectionLastTxPlainPayload_[0],
      connectionLastTxPlainLength_);
  disconnectDebug_.lastRxLlid = lastObservedRxLlid_;
  disconnectDebug_.lastRxLength = lastObservedRxLength_;
  disconnectDebug_.lastRxOpcode = lastObservedRxOpcode_;
  disconnectDebug_.lastRxNesn = lastObservedRxNesn_;
  disconnectDebug_.lastRxSn = lastObservedRxSn_;
  disconnectDebug_.lastPacketIsNew = lastObservedPacketIsNew_ ? 1U : 0U;
  disconnectDebug_.lastPeerAckedLastTx =
      lastObservedPeerAckedLastTx_ ? 1U : 0U;
}

void BleRadio::clearQueuedConnectionEvents() {
  queuedConnectionEventHead_ = 0U;
  queuedConnectionEventTail_ = 0U;
  queuedConnectionEventCount_ = 0U;
}

void BleRadio::rememberLatestConnectionRssi(const BleConnectionEvent& event) {
  if (!connected_ || !event.packetReceived || !event.crcOk) {
    return;
  }
  latestConnectionRssiDbm_ = event.rssiDbm;
  latestConnectionRssiValid_ = true;
}

bool BleRadio::shouldQueueConnectionEvent(const BleConnectionEvent& event) {
  if (!event.eventStarted) {
    return false;
  }
  if (event.terminateInd) {
    return true;
  }
  if (event.packetReceived && !event.crcOk) {
    return true;
  }
  if (event.llControlPacket || event.attPacket) {
    return true;
  }
  return event.packetReceived && (event.payloadLength > 0U);
}

bool BleRadio::enqueueConnectionEvent(const BleConnectionEvent& event) {
  if (queuedConnectionEventCount_ >= kConnectionEventQueueDepth) {
    queuedConnectionEventHead_ =
        static_cast<uint8_t>((queuedConnectionEventHead_ + 1U) %
                             kConnectionEventQueueDepth);
    --queuedConnectionEventCount_;
  }

  BleQueuedConnectionEvent& queued =
      queuedConnectionEvents_[queuedConnectionEventTail_];
  resetConnectionEventStruct(&queued.event);
  queued.event = event;
  queued.event.payload = nullptr;
  queued.event.txPayload = nullptr;

  if (event.payload != nullptr && event.payloadLength > 0U) {
    memcpy(queued.payload, event.payload, event.payloadLength);
    queued.event.payload = &queued.payload[0];
  }
  if (event.txPayload != nullptr && event.txPayloadLength > 0U) {
    memcpy(queued.txPayload, event.txPayload, event.txPayloadLength);
    queued.event.txPayload = &queued.txPayload[0];
  }

  queuedConnectionEventTail_ =
      static_cast<uint8_t>((queuedConnectionEventTail_ + 1U) %
                           kConnectionEventQueueDepth);
  ++queuedConnectionEventCount_;
  return true;
}

bool BleRadio::dequeueConnectionEvent(BleConnectionEvent* event) {
  if (event == nullptr || queuedConnectionEventCount_ == 0U) {
    return false;
  }

  BleQueuedConnectionEvent& queued =
      queuedConnectionEvents_[queuedConnectionEventHead_];
  resetConnectionEventStruct(event);
  *event = queued.event;
  event->payload = nullptr;
  event->txPayload = nullptr;

  if (queued.event.payload != nullptr && queued.event.payloadLength > 0U) {
    memcpy(queuedConnectionEventPayloadScratch_, queued.payload,
           queued.event.payloadLength);
    event->payload = &queuedConnectionEventPayloadScratch_[0];
  }
  if (queued.event.txPayload != nullptr && queued.event.txPayloadLength > 0U) {
    memcpy(queuedConnectionEventTxPayloadScratch_, queued.txPayload,
           queued.event.txPayloadLength);
    event->txPayload = &queuedConnectionEventTxPayloadScratch_[0];
  }

  queuedConnectionEventHead_ =
      static_cast<uint8_t>((queuedConnectionEventHead_ + 1U) %
                           kConnectionEventQueueDepth);
  --queuedConnectionEventCount_;
  return true;
}

bool BleRadio::configureBackgroundConnectionHardware(bool enable) {
  if (radio_ == nullptr) {
    return false;
  }

  Dppic radioDomain(nrf54l15::DPPIC10_BASE);
  Dppic periDomain(nrf54l15::DPPIC20_BASE);
  Dppic lpDomain(nrf54l15::DPPIC30_BASE);
  NRF_PPIB_Type* const ppib11 = blePpib11();
  NRF_PPIB_Type* const ppib21 = blePpib21();
  NRF_PPIB_Type* const ppib22 = blePpib22();
  NRF_PPIB_Type* const ppib30 = blePpib30();
  if (ppib11 == nullptr || ppib21 == nullptr || ppib22 == nullptr ||
      ppib30 == nullptr) {
    return false;
  }

  ppib30->EVENTS_RECEIVE[kBleBackgroundConnRxBridgeIndex] = 0U;
  ppib22->EVENTS_RECEIVE[kBleBackgroundConnRxBridgeIndex] = 0U;
  ppib21->EVENTS_RECEIVE[kBleBackgroundConnRxBridgeIndex] = 0U;
  ppib11->EVENTS_RECEIVE[kBleBackgroundConnRxBridgeIndex] = 0U;

  if (!enable) {
    (void)lpDomain.disconnectPublish(
        &NRF_GRTC->PUBLISH_COMPARE[kBleBackgroundConnRxCompareChannel]);
    (void)lpDomain.disconnectSubscribe(
        &ppib30->SUBSCRIBE_SEND[kBleBackgroundConnRxBridgeIndex]);
    (void)lpDomain.enableChannel(kBleBackgroundConnLpRxDppiChannel, false);

    (void)periDomain.disconnectPublish(
        &ppib22->PUBLISH_RECEIVE[kBleBackgroundConnRxBridgeIndex]);
    (void)periDomain.disconnectSubscribe(
        &ppib21->SUBSCRIBE_SEND[kBleBackgroundConnRxBridgeIndex]);
    (void)periDomain.enableChannel(kBleBackgroundConnPeriRxDppiChannel, false);

    (void)radioDomain.disconnectPublish(
        &ppib11->PUBLISH_RECEIVE[kBleBackgroundConnRxBridgeIndex]);
    (void)radioDomain.disconnectSubscribe(&radio_->SUBSCRIBE_RXEN);
    (void)radioDomain.enableChannel(kBleBackgroundConnRadioRxDppiChannel, false);
    return true;
  }

  if (!lpDomain.configurePublish(
          &NRF_GRTC->PUBLISH_COMPARE[kBleBackgroundConnRxCompareChannel],
          kBleBackgroundConnLpRxDppiChannel, true) ||
      !lpDomain.configureSubscribe(
          &ppib30->SUBSCRIBE_SEND[kBleBackgroundConnRxBridgeIndex],
          kBleBackgroundConnLpRxDppiChannel, true) ||
      !lpDomain.enableChannel(kBleBackgroundConnLpRxDppiChannel, true)) {
    return false;
  }

  if (!periDomain.configurePublish(
          &ppib22->PUBLISH_RECEIVE[kBleBackgroundConnRxBridgeIndex],
          kBleBackgroundConnPeriRxDppiChannel, true) ||
      !periDomain.configureSubscribe(
          &ppib21->SUBSCRIBE_SEND[kBleBackgroundConnRxBridgeIndex],
          kBleBackgroundConnPeriRxDppiChannel, true) ||
      !periDomain.enableChannel(kBleBackgroundConnPeriRxDppiChannel, true)) {
    return false;
  }

  if (!radioDomain.configurePublish(
          &ppib11->PUBLISH_RECEIVE[kBleBackgroundConnRxBridgeIndex],
          kBleBackgroundConnRadioRxDppiChannel, true) ||
      !radioDomain.configureSubscribe(&radio_->SUBSCRIBE_RXEN,
                                      kBleBackgroundConnRadioRxDppiChannel,
                                      true) ||
      !radioDomain.enableChannel(kBleBackgroundConnRadioRxDppiChannel, true)) {
    return false;
  }

  return true;
}

bool BleRadio::previewBackgroundConnectionDataChannel(
    uint8_t* outDataChannel) const {
  if (outDataChannel == nullptr || !connected_ || connectionChannelCount_ == 0U) {
    return false;
  }

  const uint16_t eventCounter = connectionEventCounter_;
  uint8_t dataChannel = 0U;
  if (connectionUseChSel2_) {
    const uint16_t prnE = bleCsa2PrnE(eventCounter, connectionChannelId_);
    dataChannel = static_cast<uint8_t>(prnE % 37U);
    if ((connectionChannelMap_[dataChannel >> 3U] &
         (1U << (dataChannel & 0x07U))) == 0U) {
      const uint8_t remapIndex =
          static_cast<uint8_t>((static_cast<uint32_t>(connectionChannelCount_) *
                                static_cast<uint32_t>(prnE)) >>
                               16U);
      dataChannel = remapChannelByIndex(connectionChannelMap_, remapIndex);
    }
  } else {
    dataChannel = static_cast<uint8_t>(
        (static_cast<uint32_t>(connectionHop_) *
         static_cast<uint32_t>(eventCounter + 1U)) %
        37U);
    if ((connectionChannelMap_[dataChannel >> 3U] &
         (1U << (dataChannel & 0x07U))) == 0U) {
      const uint8_t remapIndex =
          static_cast<uint8_t>(dataChannel % connectionChannelCount_);
      dataChannel = remapChannelByIndex(connectionChannelMap_, remapIndex);
    }
  }

  if (dataChannel > 36U) {
    return false;
  }
  *outDataChannel = dataChannel;
  return true;
}

bool BleRadio::prepareBackgroundConnectionReceiveWindow(uint32_t rxStartUs) {
  if (!initialized_ || radio_ == nullptr || !connected_ ||
      connectionRole_ != BleConnectionRole::kPeripheral ||
      (connectionCurrentRxPhy_ & kBlePhyCoded) != 0U ||
      connectionSyncAttemptsRemaining_ > 0U || connectionMissedEventCount_ > 0U ||
      connectionUpdatePending_ || connectionPhyUpdatePending_ ||
      connectionChannelMapPending_) {
    return false;
  }

  const uint32_t nowUs = bleTimingUs();
  if (timeReachedUs(nowUs + kBleBackgroundConnHardwareWakeLeadUs, rxStartUs)) {
    return false;
  }

  uint8_t dataChannel = 0U;
  if (!previewBackgroundConnectionDataChannel(&dataChannel) ||
      !setDataChannel(dataChannel)) {
    return false;
  }

  clearRadioCoreEvents(radio_);
  memset(rxPacket_, 0, sizeof(rxPacket_));
  radio_->PACKETPTR =
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&rxPacket_[0]));
  radio_->SHORTS = bleRadioShortsRxListen();

  if (!configureBackgroundConnectionHardware(true)) {
    return false;
  }

  bleProgramCompare(kBleBackgroundConnRxCompareChannel, rxStartUs, false);
  backgroundConnectionRxPrepared_ = true;
  backgroundConnectionPreparedDataChannel_ = dataChannel;
  ++encDebug_.connBgRxHardwareArmCount;
  return true;
}

void BleRadio::serviceBackgroundConnection(uint32_t spinLimit) {
  if (!initialized_ || radio_ == nullptr || !connected_ || connectionServiceBusy_) {
    return;
  }

  auto backgroundConnectionRxStartUsForNextEvent = [&]() -> uint32_t {
    if (connectionRole_ == BleConnectionRole::kPeripheral) {
      const uint32_t rxStartLeadUs =
          bleConnectionRxStartLeadUs(connectionSyncAttemptsRemaining_ > 0U,
                                     connectionMissedEventCount_);
      return (connectionNextEventUs_ > rxStartLeadUs)
                 ? static_cast<uint32_t>(connectionNextEventUs_ -
                                         rxStartLeadUs)
                 : connectionNextEventUs_;
    }
    return connectionNextEventUs_;
  };

  auto backgroundConnectionEventWakeUsForNextEvent = [&]() -> uint32_t {
    const uint32_t radioStartUs = backgroundConnectionRxStartUsForNextEvent();

    uint32_t wakeUs =
        (radioStartUs > kBleConnAnchorPrewaitUs)
            ? static_cast<uint32_t>(radioStartUs - kBleConnAnchorPrewaitUs)
            : radioStartUs;
    return wakeUs;
  };

  auto backgroundConnectionPrewarmUsForEventWake = [](uint32_t eventWakeUs)
      -> uint32_t {
    return (eventWakeUs > kBleBackgroundConnDefaultHfxoLeadUs)
               ? static_cast<uint32_t>(eventWakeUs -
                                       kBleBackgroundConnDefaultHfxoLeadUs)
               : eventWakeUs;
  };

  auto armBackgroundConnectionForNextEvent = [&](uint32_t nowUs) {
    uint32_t eventWakeUs = backgroundConnectionEventWakeUsForNextEvent();
    const uint32_t rxStartUs = backgroundConnectionRxStartUsForNextEvent();
    uint32_t prewarmBasisUs = eventWakeUs;
    backgroundConnectionRxPrepared_ = false;
    backgroundConnectionPreparedDataChannel_ = 0U;
    bleDisableCompare(kBleBackgroundConnRxCompareChannel, true);

    const uint32_t preparedWakeUs =
        (connectionNextEventUs_ > kBleBackgroundConnHardwareWakeLeadUs)
            ? static_cast<uint32_t>(connectionNextEventUs_ -
                                    kBleBackgroundConnHardwareWakeLeadUs)
            : connectionNextEventUs_;
    if (!timeReachedUs(nowUs, preparedWakeUs) &&
        prepareBackgroundConnectionReceiveWindow(rxStartUs)) {
      eventWakeUs = rxStartUs + kBleBackgroundConnRxFallbackLeadUs;
      prewarmBasisUs = rxStartUs;
    }

    const uint32_t prewarmUs =
        backgroundConnectionPrewarmUsForEventWake(prewarmBasisUs);
    bleDisableCompare(kBleBackgroundConnPrewarmCompareChannel, true);

    if (!bleHfxoRunning() && (prewarmUs < eventWakeUs) &&
        !timeReachedUs(nowUs, prewarmUs)) {
      backgroundConnectionWakeUs_ = prewarmUs;
      backgroundConnectionEventWakeArmed_ = false;
      backgroundConnectionServiceArmed_ = true;
      backgroundConnectionServiceDue_ = false;
      g_bleBackgroundRadio = this;
      bleProgramCompare(kBleBackgroundConnPrewarmCompareChannel, prewarmUs,
                        true);
      bleBackgroundEnableIrq();
      return;
    }

    if (!bleHfxoRunning() && timeReachedUs(nowUs, prewarmUs)) {
      (void)ClockControl::startHfxo(false, 0U);
      // Match the HFXO prewarm window with the XIAO RF switch settle window.
      // Enabling RF_SW only at event wake can depress the first RSSI sample
      // even though packet timing still succeeds.
      (void)ensureRfPathActiveForBle();
    }

    if (timeReachedUs(nowUs, eventWakeUs)) {
      backgroundConnectionWakeUs_ = eventWakeUs;
      backgroundConnectionEventWakeArmed_ = false;
      backgroundConnectionServiceArmed_ = false;
      backgroundConnectionServiceDue_ = true;
      return;
    }

    backgroundConnectionWakeUs_ = eventWakeUs;
    backgroundConnectionEventWakeArmed_ = true;
    backgroundConnectionServiceArmed_ = true;
    backgroundConnectionServiceDue_ = false;
    g_bleBackgroundRadio = this;
    bleProgramCompare(kBleBackgroundConnPrewarmCompareChannel, eventWakeUs,
                      true);
    bleBackgroundEnableIrq();
  };

  if (spinLimit == 0U) {
    const uint32_t nowUs = bleTimingUs();
    if (!backgroundConnectionServiceDue_ &&
        !timeReachedUs(nowUs, connectionNextEventUs_)) {
      if (backgroundConnectionServiceEnabled_ && connectionNextEventUs_ != 0U &&
          !backgroundConnectionServiceArmed_) {
        armBackgroundConnectionForNextEvent(nowUs);
      }
      return;
    }
  }

  const bool manageConnectionHfxo = backgroundConnectionServiceEnabled_;
  if (!ensureRfPathActiveForBle()) {
    return;
  }
  if (manageConnectionHfxo && !ClockControl::startHfxo(
                                  true, (spinLimit == 0U) ? 1500000UL : spinLimit)) {
    releaseRfPathForBle();
    return;
  }

  ++smpSecureConnectionsBackgroundServiceCount_;
  connectionServiceBusy_ = true;
  backgroundConnectionEventWakeArmed_ = false;
  backgroundConnectionServiceInProgress_ = backgroundConnectionServiceEnabled_;
  BleConnectionEvent event{};
  resetConnectionEventStruct(&event);
  const bool ran = pollConnectionEventInternal(&event, spinLimit);
  if (ran) {
    rememberLatestConnectionRssi(event);
  }
  if (ran && shouldQueueConnectionEvent(event)) {
    enqueueConnectionEvent(event);
  }
  backgroundConnectionServiceInProgress_ = false;
  connectionServiceBusy_ = false;
  if (manageConnectionHfxo && bleHfxoRunning()) {
    ClockControl::stopHfxo();
  }
  releaseRfPathForBle();

  if (backgroundConnectionServiceEnabled_ && connected_ && connectionNextEventUs_ != 0U) {
    const uint32_t nowUs = bleTimingUs();
    armBackgroundConnectionForNextEvent(nowUs);
  }

  // When background connection service is already running in thread context,
  // deliver deferred peer-write callbacks immediately instead of waiting for a
  // later idle/yield pass. This keeps simple foreground loops such as NUS
  // bridges from depending on an extra service call just to surface bytes
  // that were already accepted and ACKed at the ATT layer.
  if (!bleRunningInIsr()) {
    dispatchDeferredGattWrites();
  }
}

bool BleRadio::disconnect(uint32_t spinLimit) {
  return disconnectWithReason(BleDisconnectReason::kApi, 0U, spinLimit);
}

void BleRadio::setCentralMissingPeerDisconnectEvents(uint16_t events) {
  centralMissingPeerDisconnectEvents_ = events;
  g_ble_central_missing_peer_threshold = events;
}

uint16_t BleRadio::centralMissingPeerDisconnectEvents() const {
  return centralMissingPeerDisconnectEvents_;
}

bool BleRadio::disconnectWithReason(BleDisconnectReason reason, uint8_t errorCode,
                                    uint32_t spinLimit) {
  if (!initialized_ || radio_ == nullptr) {
    return false;
  }

  recordDisconnectDebug(reason, errorCode);
  radio_->SHORTS = 0U;
  radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
  waitDisabled(spinLimit);
  connected_ = false;
  flushDeferredBondStorage();
  connectionRole_ = BleConnectionRole::kNone;
  backgroundConnectionServiceEnabled_ = false;
  backgroundConnectionServiceArmed_ = false;
  backgroundConnectionServiceInProgress_ = false;
  backgroundConnectionServiceDue_ = false;
  backgroundConnectionEventWakeArmed_ = false;
  backgroundConnectionWakeUs_ = 0U;
  g_bleBackgroundPendSvPending = 0U;
  blePendSvClearPending();
  backgroundConnectionRxPrepared_ = false;
  backgroundConnectionPreparedDataChannel_ = 0U;
  deferredConnectionEventHead_ = 0U;
  deferredConnectionEventTail_ = 0U;
  deferredConnectionEventCount_ = 0U;
  deferredGattWriteHead_ = 0U;
  deferredGattWriteTail_ = 0U;
  deferredGattWriteCount_ = 0U;
  deferredTraceHead_ = 0U;
  deferredTraceTail_ = 0U;
  deferredTraceCount_ = 0U;
  bleDisableCompare(kBleBackgroundConnPrewarmCompareChannel, true);
  bleDisableCompare(kBleBackgroundConnRxCompareChannel, true);
  (void)configureBackgroundConnectionHardware(false);
  if (g_bleBackgroundRadio == this && !backgroundAdvertisingEnabled_) {
    g_bleBackgroundRadio = nullptr;
  }
  connectionLastTxLlid_ = 0x01U;
  connectionLastTxLength_ = 0U;
  connectionLastTxSn_ = 0U;
  connectionTxHistoryValid_ = false;
  connectionLastTxMoreData_ = false;
  connectionFreshTxAllowed_ = true;
  connectionLastTxPlainLlid_ = 0x01U;
  connectionLastTxPlainLength_ = 0U;
  memset(connectionLastTxPlainPayload_, 0, sizeof(connectionLastTxPlainPayload_));
  connectionPendingTxLlid_ = 0x01U;
  connectionPendingTxLength_ = 0U;
  connectionPendingTxValid_ = false;
  connectionPendingTxSmpFragment_ = false;
  memset(connectionPendingTxPayload_, 0, sizeof(connectionPendingTxPayload_));
  clearPendingL2capTxFragment();
  connectionAttMtu_ = kBleDefaultAttMtu;
  connectionUseChSel2_ = false;
  connectionChannelId_ = 0U;
  connectionMissedEventCount_ = 0U;
  connectionFirstEventListenUs_ = 0U;
  connectionSyncAttemptsRemaining_ = 0U;
  connectionUpdatePending_ = false;
  connectionUpdateInstant_ = 0U;
  connectionPendingWinSize_ = 0U;
  connectionPendingWinOffset_ = 0U;
  connectionPendingIntervalUnits_ = 0U;
  connectionPendingLatency_ = 0U;
  connectionPendingTimeoutUnits_ = 0U;
  connectionCentralConnParamIndPending_ = false;
  connectionCentralConnParamIntervalUnits_ = 0U;
  connectionCentralConnParamLatency_ = 0U;
  connectionCentralConnParamTimeoutUnits_ = 0U;
  connectionChannelMapPending_ = false;
  connectionChannelMapInstant_ = 0U;
  memset(connectionPendingChannelMap_, 0, sizeof(connectionPendingChannelMap_));
  connectionPendingChannelCount_ = 0U;
  connectionServiceChangedIndicationsEnabled_ = false;
  connectionServiceChangedCccdRestored_ = false;
  connectionServiceChangedIndicationPending_ = false;
  connectionServiceChangedIndicationAwaitingConfirm_ = false;
  connectionServiceChangedIndicationStartHandle_ = 0U;
  connectionServiceChangedIndicationEndHandle_ = 0U;
  connectionServiceChangedIndicationGeneration_ = 0U;
  connectionBatteryNotificationsEnabled_ = false;
  connectionBatteryNotificationPending_ = false;
  latestConnectionRssiDbm_ = 0;
  latestConnectionRssiValid_ = false;
  clearPreparedWriteState();
  clearCustomGattConnectionState();
  clearConnectionSecurityState();
  connectionServiceBusy_ = false;
  clearQueuedConnectionEvents();
  if (!configureBle1M()) {
    restoreAdvertisingLinkDefaults();
  }
  clearRadioCoreEvents(radio_);
  endUnconnectedRadioActivity();
  switch (reason) {
    case BleDisconnectReason::kApi:
      emitBleTrace("DISCONNECT_API");
      break;
    case BleDisconnectReason::kSupervisionTimeout:
      emitBleTrace("DISCONNECT_SUPERVISION_TIMEOUT");
      break;
    case BleDisconnectReason::kPeerTerminate:
      emitBleTrace("DISCONNECT_PEER_TERMINATE");
      break;
    case BleDisconnectReason::kMicFailure:
      emitBleTrace("DISCONNECT_MIC_FAILURE");
      break;
    case BleDisconnectReason::kInternalTerminate:
      emitBleTrace("DISCONNECT_INTERNAL");
      break;
    default:
      emitBleTrace("DISCONNECT_REASON_UNKNOWN");
      break;
  }
  switch (errorCode) {
    case 0x00U:
      break;
    case kBleLlErrorConnectionTimeout:
      emitBleTrace("DISCONNECT_ERR_TIMEOUT_08");
      break;
    case 0x13U:
      emitBleTrace("DISCONNECT_ERR_REMOTE_USER_13");
      break;
    case 0x16U:
      emitBleTrace("DISCONNECT_ERR_LOCAL_HOST_16");
      break;
    case 0x21U:
      emitBleTrace("DISCONNECT_ERR_LL_RESPONSE_TIMEOUT_21");
      break;
    case kBleLlErrorMicFailure:
      emitBleTrace("DISCONNECT_ERR_MIC_3D");
      break;
    default:
      emitBleTrace("DISCONNECT_ERR_OTHER");
      break;
  }
  emitBleTrace("DISCONNECT_DBG_LAST_RX_LLID");
  emitBleTraceHexByte(disconnectDebug_.lastRxLlid);
  emitBleTrace("DISCONNECT_DBG_LAST_RX_LEN");
  emitBleTraceHexByte(disconnectDebug_.lastRxLength);
  emitBleTrace("DISCONNECT_DBG_LAST_RX_OPCODE");
  emitBleTraceHexByte(disconnectDebug_.lastRxOpcode);
  emitBleTrace("DISCONNECT_DBG_LAST_TX_LLID");
  emitBleTraceHexByte(disconnectDebug_.lastTxLlid);
  emitBleTrace("DISCONNECT_DBG_LAST_TX_LEN");
  emitBleTraceHexByte(disconnectDebug_.lastTxLength);
  emitBleTrace("DISCONNECT_DBG_LAST_TX_OPCODE");
  emitBleTraceHexByte(disconnectDebug_.lastTxOpcode);
  emitBleTrace("DISCONNECT_DBG_PENDING_TX");
  emitBleTraceHexByte(disconnectDebug_.pendingTxValid);
  emitBleTrace("DISCONNECT_DBG_PENDING_OPCODE");
  emitBleTraceHexByte(disconnectDebug_.pendingTxValid
                          ? disconnectDebug_.pendingTxOpcode
                          : 0U);
  emitBleTrace("DISCONNECT");
  return true;
}

bool BleRadio::consumeDeferredConnectionEvent(BleConnectionEvent* event) {
  if (event == nullptr) {
    return false;
  }

  resetConnectionEventStruct(event);
  return dequeueConnectionEvent(event);
}

void BleRadio::snapshotDeferredConnectionEvent(const BleConnectionEvent& event) {
  if (!shouldQueueConnectionEvent(event)) {
    return;
  }

  const uint32_t irqState = bleEnterCritical();
  (void)enqueueConnectionEvent(event);
  if (g_activeBleRadio != this) {
    g_bleBackgroundRadio = this;
  } else if (g_bleBackgroundRadio == this) {
    g_bleBackgroundRadio = nullptr;
  }
  bleExitCritical(irqState);
}

bool BleRadio::enqueueDeferredGattWrite(BleGattWriteCallback callback, void* context,
                                        uint16_t valueHandle, const uint8_t* value,
                                        uint16_t valueLength, bool withResponse) {
  if (callback == nullptr || valueLength > kCustomGattMaxValueLength ||
      (valueLength > 0U && value == nullptr)) {
    return false;
  }

  const uint8_t depth = static_cast<uint8_t>(
      sizeof(deferredGattWrites_) / sizeof(deferredGattWrites_[0]));
  const uint32_t irqState = bleEnterCritical();
  if (deferredGattWriteCount_ >= depth) {
    bleExitCritical(irqState);
    ++encDebug_.connDeferredCallbackDropCount;
    return false;
  }

  BleDeferredGattWriteState& slot = deferredGattWrites_[deferredGattWriteTail_];
  slot.callback = callback;
  slot.context = context;
  slot.valueHandle = valueHandle;
  slot.valueLength = valueLength;
  slot.withResponse = withResponse ? 1U : 0U;
  slot.signedWrite = 0U;
  slot.signCounter = 0U;
  if (valueLength > 0U) {
    memcpy(slot.value, value, valueLength);
  }
  if (valueLength < sizeof(slot.value)) {
    memset(&slot.value[valueLength], 0, sizeof(slot.value) - valueLength);
  }

  deferredGattWriteTail_ =
      static_cast<uint8_t>((deferredGattWriteTail_ + 1U) % depth);
  ++deferredGattWriteCount_;
  // Deferred peer-write callbacks for the currently active radio do not need
  // the shared background GRTC owner slot unless a real background BLE
  // service is armed. Claiming that slot unnecessarily keeps the IRQ path
  // pointed at background advertising/connection cleanup even after the link
  // has already dropped, which can starve the foreground code that should
  // consume the queued connection event carrying the disconnect/ATT cleanup.
  if (g_activeBleRadio != this || backgroundServicesActive()) {
    g_bleBackgroundRadio = this;
  }
  bleExitCritical(irqState);
  return true;
}

bool BleRadio::enqueueDeferredSignedGattWrite(uint16_t valueHandle,
                                              const uint8_t* value,
                                              uint16_t valueLength,
                                              uint32_t signCounter) {
  if (valueLength > kCustomGattMaxValueLength ||
      (valueLength > 0U && value == nullptr)) {
    return false;
  }
  const uint8_t depth = static_cast<uint8_t>(
      sizeof(deferredGattWrites_) / sizeof(deferredGattWrites_[0]));
  const uint32_t irqState = bleEnterCritical();
  if (deferredGattWriteCount_ >= depth) {
    bleExitCritical(irqState);
    ++encDebug_.connDeferredCallbackDropCount;
    return false;
  }
  BleDeferredGattWriteState& slot = deferredGattWrites_[deferredGattWriteTail_];
  memset(&slot, 0, sizeof(slot));
  slot.context = this;
  slot.valueHandle = valueHandle;
  slot.valueLength = valueLength;
  slot.signedWrite = 1U;
  slot.signCounter = signCounter;
  if (valueLength > 0U) {
    memcpy(slot.value, value, valueLength);
  }
  deferredGattWriteTail_ =
      static_cast<uint8_t>((deferredGattWriteTail_ + 1U) % depth);
  ++deferredGattWriteCount_;
  if (g_activeBleRadio != this || backgroundServicesActive()) {
    g_bleBackgroundRadio = this;
  }
  bleExitCritical(irqState);
  return true;
}

void BleRadio::dispatchDeferredGattWrites() {
  while (true) {
    BleDeferredGattWriteState slot{};
    const uint8_t depth = static_cast<uint8_t>(
        sizeof(deferredGattWrites_) / sizeof(deferredGattWrites_[0]));
    const uint32_t irqState = bleEnterCritical();
    if (deferredGattWriteCount_ == 0U) {
      bleExitCritical(irqState);
      return;
    }

    slot = deferredGattWrites_[deferredGattWriteHead_];
    memset(&deferredGattWrites_[deferredGattWriteHead_], 0,
           sizeof(deferredGattWrites_[deferredGattWriteHead_]));
    deferredGattWriteHead_ =
        static_cast<uint8_t>((deferredGattWriteHead_ + 1U) % depth);
    --deferredGattWriteCount_;
    bleExitCritical(irqState);

    if (slot.signedWrite != 0U && slot.context == this) {
      applyDeferredSignedGattWrite(slot.valueHandle, slot.value,
                                   slot.valueLength, slot.signCounter);
    } else if (slot.callback != nullptr) {
      slot.callback(slot.valueHandle, slot.value, slot.valueLength,
                    slot.withResponse != 0U, slot.context);
    }
  }
}

bool BleRadio::enqueueDeferredTrace(const char* message) {
  if (message == nullptr) {
    return false;
  }

  const uint8_t depth =
      static_cast<uint8_t>(sizeof(deferredTraces_) / sizeof(deferredTraces_[0]));
  const uint32_t irqState = bleEnterCritical();
  if (deferredTraceCount_ >= depth) {
    bleExitCritical(irqState);
    ++encDebug_.connDeferredTraceDropCount;
    return false;
  }

  deferredTraces_[deferredTraceTail_] = message;
  deferredTraceTail_ = static_cast<uint8_t>((deferredTraceTail_ + 1U) % depth);
  ++deferredTraceCount_;
  if (g_activeBleRadio != this || backgroundServicesActive()) {
    g_bleBackgroundRadio = this;
  }
  bleExitCritical(irqState);
  return true;
}

void BleRadio::dispatchDeferredTraces() {
#if defined(NRF54L15_CLEAN_BLE_TRACE) && (NRF54L15_CLEAN_BLE_TRACE == 1)
  while (true) {
    const uint8_t depth =
        static_cast<uint8_t>(sizeof(deferredTraces_) / sizeof(deferredTraces_[0]));
    const uint32_t irqState = bleEnterCritical();
    if (deferredTraceCount_ == 0U) {
      bleExitCritical(irqState);
      return;
    }

    const char* message = deferredTraces_[deferredTraceHead_];
    deferredTraces_[deferredTraceHead_] = nullptr;
    deferredTraceHead_ =
        static_cast<uint8_t>((deferredTraceHead_ + 1U) % depth);
    --deferredTraceCount_;
    bleExitCritical(irqState);

    if (message == nullptr) {
      continue;
    }
    if (traceCallback_ != nullptr) {
      traceCallback_(message, traceCallbackContext_);
      continue;
    }
    Serial.print(F("[BLE] "));
    Serial.println(message);
  }
#endif
}

bool BleRadio::backgroundServicesActive() const {
  return backgroundAdvertisingEnabled_ || backgroundConnectionServiceEnabled_;
}

void BleRadio::serviceDeferredApplicationWork() {
  if (bleRunningInIsr()) {
    return;
  }

  serviceCustomGattAuthorizationTimeout();
  serviceSecureConnectionsWork();
  serviceSmpKeyDistribution();
  (void)completeSmpKeyDistributionIfDone();
  bool postponeNonCriticalWork = false;
  if (connected_) {
    const uint32_t nowUs = bleTimingUs();
    uint32_t intervalUs =
        (connectionIntervalUnits_ > 0U)
            ? (static_cast<uint32_t>(connectionIntervalUnits_) * 1250UL)
            : 1250UL;
    uint32_t guardUs = intervalUs / 3U;
    if (guardUs < 3000U) {
      guardUs = 3000U;
    } else if (guardUs > 12000U) {
      guardUs = 12000U;
    }
    postponeNonCriticalWork = timeReachedUs(nowUs + guardUs, connectionNextEventUs_);
  }

  if (!postponeNonCriticalWork) {
    if (bondFlashPersistPending_ || bondSigningCounterPersistPending_ ||
        cccdFlashPersistPending_) {
      (void)flushDeferredBondStorage();
    }
    if (connected_ &&
        (!smpPrefetchedLocalRandomValid_ ||
         !connectionPrefetchedEncMaterialValid_)) {
      prefetchConnectionSecurityMaterial(kBleSecurityRandomSpinLimit);
    }
    const uint32_t migrationNowUs = bleTimingUs();
    if (connected_ && bondLocalIdentityMigrationPending_ &&
        (bondLocalIdentityMigrationRetryAtUs_ == 0U ||
         timeReachedUs(migrationNowUs,
                       bondLocalIdentityMigrationRetryAtUs_))) {
      const BleBondRecord normalizedRecord = bondRecord_;
      if (persistBondRecord(normalizedRecord)) {
        bondLocalIdentityMigrationPending_ = false;
        bondLocalIdentityMigrationRetryAtUs_ = 0U;
      } else {
        bondLocalIdentityMigrationRetryAtUs_ = migrationNowUs + 1000000UL;
        emitBleTrace("BOND_LOCAL_RPA_MIGRATE_PERSIST_FAILED");
      }
    }
    dispatchDeferredGattWrites();
    dispatchDeferredTraces();
  }

  const uint32_t irqState = bleEnterCritical();
  if (!backgroundServicesActive() && g_bleBackgroundRadio == this &&
      queuedConnectionEventCount_ == 0U &&
      deferredConnectionEventCount_ == 0U &&
      deferredGattWriteCount_ == 0U && deferredTraceCount_ == 0U) {
    g_bleBackgroundRadio = nullptr;
  }
  bleExitCritical(irqState);
}

void BleRadio::setBackgroundConnectionServiceEnabled(bool enabled) {
  if (!enabled) {
    stopBackgroundConnectionService();
    return;
  }
  const uint32_t irqState = bleEnterCritical();
  if (!initialized_ || radio_ == nullptr || !connected_) {
    backgroundConnectionServiceEnabled_ = false;
    backgroundConnectionServiceArmed_ = false;
    backgroundConnectionServiceInProgress_ = false;
    backgroundConnectionServiceDue_ = false;
    backgroundConnectionEventWakeArmed_ = false;
    backgroundConnectionRxPrepared_ = false;
    backgroundConnectionPreparedDataChannel_ = 0U;
    backgroundConnectionWakeUs_ = 0U;
    bleExitCritical(irqState);
    return;
  }

  backgroundConnectionServiceEnabled_ = true;
  backgroundConnectionServiceArmed_ = false;
  backgroundConnectionServiceInProgress_ = false;
  backgroundConnectionServiceDue_ = false;
  backgroundConnectionEventWakeArmed_ = false;
  backgroundConnectionRxPrepared_ = false;
  backgroundConnectionPreparedDataChannel_ = 0U;
  backgroundConnectionWakeUs_ = 0U;
  g_bleBackgroundPendSvPending = 0U;
  blePendSvClearPending();
  bleDisableCompare(kBleBackgroundConnPrewarmCompareChannel, true);
  bleDisableCompare(kBleBackgroundConnRxCompareChannel, true);
  (void)configureBackgroundConnectionHardware(false);
  NVIC->ICPR[static_cast<uint32_t>(kBleBackgroundGrtcIrq) >> 5U] =
      (1UL << (static_cast<uint32_t>(kBleBackgroundGrtcIrq) & 0x1FUL));
  if (g_bleBackgroundRadio == this && !backgroundAdvertisingEnabled_ &&
      queuedConnectionEventCount_ == 0U &&
      deferredConnectionEventCount_ == 0U &&
      deferredGattWriteCount_ == 0U && deferredTraceCount_ == 0U) {
    g_bleBackgroundRadio = nullptr;
  }
  bleExitCritical(irqState);
  if (bleHfxoRunning()) {
    ClockControl::stopHfxo();
  }
  releaseRfPathForBle();
}

bool BleRadio::isBackgroundConnectionServiceEnabled() const {
  return backgroundConnectionServiceEnabled_;
}

void BleRadio::serviceBackgroundConnectionFromIrq() {
  if (!backgroundConnectionServiceEnabled_ ||
      !bleCompareEventPending(kBleBackgroundConnPrewarmCompareChannel)) {
    return;
  }

  bleDisableCompare(kBleBackgroundConnPrewarmCompareChannel, true);
  backgroundConnectionServiceArmed_ = false;

  if (!backgroundConnectionEventWakeArmed_) {
    (void)ClockControl::startHfxo(false, 0U);
    // Keep RF_SW on only for the radio prewarm/event window. This avoids the
    // idle-current regression from holding the switch through delay(), but
    // gives the antenna path time to settle before RSSI is sampled.
    (void)ensureRfPathActiveForBle();

    uint32_t radioStartUs = connectionNextEventUs_;
    if (connectionRole_ == BleConnectionRole::kPeripheral) {
      const uint32_t rxStartLeadUs =
          bleConnectionRxStartLeadUs(connectionSyncAttemptsRemaining_ > 0U,
                                     connectionMissedEventCount_);
      radioStartUs = (connectionNextEventUs_ > rxStartLeadUs)
                         ? static_cast<uint32_t>(connectionNextEventUs_ -
                                                 rxStartLeadUs)
                         : connectionNextEventUs_;
    }
    const uint32_t eventWakeUs =
        backgroundConnectionRxPrepared_
            ? static_cast<uint32_t>(radioStartUs +
                                    kBleBackgroundConnRxFallbackLeadUs)
            : ((radioStartUs > kBleConnAnchorPrewaitUs)
                   ? static_cast<uint32_t>(radioStartUs -
                                           kBleConnAnchorPrewaitUs)
                   : radioStartUs);
    const uint32_t nowUs = bleTimingUs();
    if (!timeReachedUs(nowUs, eventWakeUs)) {
      backgroundConnectionWakeUs_ = eventWakeUs;
      backgroundConnectionEventWakeArmed_ = true;
      backgroundConnectionServiceDue_ = false;
      backgroundConnectionServiceArmed_ = true;
      g_bleBackgroundRadio = this;
      bleProgramCompare(kBleBackgroundConnPrewarmCompareChannel, eventWakeUs,
                        true);
      bleBackgroundEnableIrq();
      return;
    }
  }

  backgroundConnectionEventWakeArmed_ = false;
  backgroundConnectionServiceDue_ = true;
  serviceBackgroundConnection(120000UL);
}

void BleRadio::stopBackgroundConnectionService() {
  if (NRF54L15_CLEAN_BLE_BACKGROUND_SERVICE == 0) {
    return;
  }

  const uint32_t irqState = bleEnterCritical();
  backgroundConnectionServiceEnabled_ = false;
  backgroundConnectionServiceArmed_ = false;
  backgroundConnectionServiceInProgress_ = false;
  backgroundConnectionServiceDue_ = false;
  backgroundConnectionEventWakeArmed_ = false;
  backgroundConnectionRxPrepared_ = false;
  backgroundConnectionPreparedDataChannel_ = 0U;
  backgroundConnectionWakeUs_ = 0U;
  g_bleBackgroundPendSvPending = 0U;
  blePendSvClearPending();
  bleDisableCompare(kBleBackgroundConnPrewarmCompareChannel, false);
  bleDisableCompare(kBleBackgroundConnRxCompareChannel, true);
  (void)configureBackgroundConnectionHardware(false);
  if (g_bleBackgroundRadio == this && !backgroundAdvertisingEnabled_ &&
      queuedConnectionEventCount_ == 0U &&
      deferredConnectionEventCount_ == 0U &&
      deferredGattWriteCount_ == 0U && deferredTraceCount_ == 0U) {
    g_bleBackgroundRadio = nullptr;
  }
  bleExitCritical(irqState);
  releaseRfPathForBle();
}
