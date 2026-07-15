namespace {

bool localHasPeerSecureConnectionsOobData(bool localInitiator,
                                          const uint8_t pairingReq[7],
                                          const uint8_t pairingRsp[7]) {
  // The OOB flag means "I have the peer's OOB authentication data". It does
  // not advertise locally generated OOB data.
  return localInitiator ? (pairingReq[2] != 0U) : (pairingRsp[2] != 0U);
}

bool peerHasLocalSecureConnectionsOobData(bool localInitiator,
                                          const uint8_t pairingReq[7],
                                          const uint8_t pairingRsp[7]) {
  return localInitiator ? (pairingRsp[2] != 0U) : (pairingReq[2] != 0U);
}

bool validateSecureConnectionsOobCommitment(
    const uint8_t peerPublicKey[65], const BleOobData& remoteOob) {
  uint8_t expectedConfirm[16] = {0};
  return bleSecureConnectionsF4(&peerPublicKey[1], &peerPublicKey[1],
                                remoteOob.r, 0x00U, expectedConfirm) &&
         (memcmp(expectedConfirm, remoteOob.c, sizeof(expectedConfirm)) == 0);
}

void invalidateSecureConnectionsKeypairAndOob(
    bool& localKeyReady, bool& localKeyInProgress,
    uint8_t (&privateKey)[32], uint8_t (&publicKey)[65],
    bool& localOobValid, bool& remoteOobValid,
    BleOobData& localOob, BleOobData& remoteOob) {
  localKeyReady = false;
  localKeyInProgress = false;
  memset(privateKey, 0, sizeof(privateKey));
  memset(publicKey, 0, sizeof(publicKey));

  // c is bound to the retired public key, and the peer is required to retire
  // its key after this pairing attempt too. Never reuse either OOB payload.
  localOobValid = false;
  remoteOobValid = false;
  memset(&localOob, 0, sizeof(localOob));
  memset(&remoteOob, 0, sizeof(remoteOob));
}

}  // namespace

namespace {

constexpr uint32_t kGattAuthorizationTimeoutMs = 30000UL;

bool gattAuthorizeStatusToAttError(uint16_t gattStatus,
                                   uint8_t* outError) {
  if (outError == nullptr) {
    return false;
  }
  if (gattStatus == 0U) {
    *outError = 0U;
    return true;
  }
  if ((gattStatus & 0xFF00U) != 0x0100U ||
      (gattStatus & 0x00FFU) == 0U) {
    return false;
  }
  *outError = static_cast<uint8_t>(gattStatus & 0x00FFU);
  return true;
}

}  // namespace

bool BleRadio::buildAttErrorResponse(uint8_t requestOpcode, uint16_t handle,
                                     uint8_t errorCode, uint8_t* outAttResponse,
                                     uint16_t* outAttResponseLength) const {
  if (outAttResponse == nullptr || outAttResponseLength == nullptr) {
    return false;
  }

  emitAttErrorTrace(requestOpcode, handle, errorCode);
  outAttResponse[0] = kAttOpErrorRsp;
  outAttResponse[1] = requestOpcode;
  writeLe16(&outAttResponse[2], handle);
  outAttResponse[4] = errorCode;
  *outAttResponseLength = 5U;
  return true;
}

static uint8_t attReadSentinelToError(uint8_t readLength,
                                      uint8_t notPermittedError) {
  if (readLength == kAttrReadInvalidHandleLen) {
    return kAttErrInvalidHandle;
  }
  if (readLength == kAttrReadInvalidOffsetLen) {
    return kAttErrInvalidOffset;
  }
  if (readLength == kAttrReadInsufficientEncryptionLen) {
    return kAttErrInsufficientEncryption;
  }
  if (readLength == kAttrReadInsufficientAuthenticationLen) {
    return kAttErrInsufficientAuthentication;
  }
  if (readLength == kAttrReadNotPermittedLen) {
    return notPermittedError;
  }
  return 0U;
}

static uint8_t attPermissionErrorToReadSentinel(uint8_t errorCode) {
  if (errorCode == kAttErrInsufficientEncryption) {
    return kAttrReadInsufficientEncryptionLen;
  }
  if (errorCode == kAttErrInsufficientAuthentication) {
    return kAttrReadInsufficientAuthenticationLen;
  }
  if (errorCode == kAttErrInvalidHandle) {
    return kAttrReadInvalidHandleLen;
  }
  return kAttrReadNotPermittedLen;
}

void BleRadio::clearPreparedWriteState() {
  connectionPreparedWriteActive_ = false;
  connectionPreparedWriteHandle_ = 0U;
  memset(connectionPreparedWriteValue_, 0, sizeof(connectionPreparedWriteValue_));
  connectionPreparedWriteLength_ = 0U;
  connectionPreparedWriteMask_ = 0U;
  connectionPreparedWriteIsCustomVariable_ = false;
}

bool BleRadio::beginCustomGattAuthorization(
    BleGattAuthorizeType type, uint8_t requestOpcode, uint8_t operation,
    const BleCustomCharacteristicState* characteristic, uint16_t offset,
    const uint8_t* value, uint16_t valueLength) {
  if (characteristic == nullptr || type == BleGattAuthorizeType::kInvalid ||
      valueLength > kCustomGattMaxValueLength ||
      (valueLength > 0U && value == nullptr)) {
    return false;
  }

  const uint32_t irqState = bleEnterCritical();
  if (connectionGattAuthorization_.pending ||
      connectionGattAuthorization_.responseReady) {
    bleExitCritical(irqState);
    return false;
  }

  clearCustomGattAuthorizationState();
  ++connectionGattAuthorizationGeneration_;
  if (connectionGattAuthorizationGeneration_ == 0U) {
    ++connectionGattAuthorizationGeneration_;
  }
  connectionGattAuthorization_.pending = true;
  connectionGattAuthorization_.callbackPending = true;
  connectionGattAuthorization_.type = type;
  connectionGattAuthorization_.operation = operation;
  connectionGattAuthorization_.requestOpcode = requestOpcode;
  connectionGattAuthorization_.uuidSize = characteristic->uuidSize;
  connectionGattAuthorization_.handle = characteristic->valueHandle;
  connectionGattAuthorization_.offset = offset;
  connectionGattAuthorization_.valueLength = valueLength;
  connectionGattAuthorization_.generation =
      connectionGattAuthorizationGeneration_;
  connectionGattAuthorization_.deadlineMs = millis() +
                                               kGattAuthorizationTimeoutMs;
  memcpy(connectionGattAuthorization_.uuid, characteristic->uuid,
         sizeof(connectionGattAuthorization_.uuid));
  if (valueLength > 0U) {
    memcpy(connectionGattAuthorization_.value, value, valueLength);
  }
  bleExitCritical(irqState);
  emitBleTrace(type == BleGattAuthorizeType::kRead
                   ? "ATT_READ_AUTHORIZE_DEFER"
                   : "ATT_WRITE_AUTHORIZE_DEFER");
  return true;
}

BleGattAuthorizeReplyResult BleRadio::replyCustomGattAuthorization(
    BleGattAuthorizeType type, uint16_t gattStatus, bool update,
    uint16_t offset, const uint8_t* value, uint16_t valueLength) {
  if (!connected_ || connectionRole_ != BleConnectionRole::kPeripheral) {
    return BleGattAuthorizeReplyResult::kInvalidConnection;
  }
  if (valueLength > kCustomGattMaxValueLength ||
      (valueLength > 0U && value == nullptr)) {
    return BleGattAuthorizeReplyResult::kInvalidParameter;
  }

  uint8_t attError = 0U;
  if (!gattAuthorizeStatusToAttError(gattStatus, &attError)) {
    return BleGattAuthorizeReplyResult::kInvalidParameter;
  }

  const uint32_t irqState = bleEnterCritical();
  if (!connectionGattAuthorization_.pending ||
      connectionGattAuthorization_.type != type) {
    bleExitCritical(irqState);
    return BleGattAuthorizeReplyResult::kInvalidState;
  }
  const bool timedOut = static_cast<int32_t>(
                            millis() -
                            connectionGattAuthorization_.deadlineMs) >= 0;
  const uint16_t requestHandle = connectionGattAuthorization_.handle;
  const uint16_t requestOffset = connectionGattAuthorization_.offset;
  const uint8_t requestOpcode =
      connectionGattAuthorization_.requestOpcode;
  const uint32_t requestGeneration =
      connectionGattAuthorization_.generation;
  bleExitCritical(irqState);

  if (timedOut) {
    const uint32_t timeoutIrqState = bleEnterCritical();
    const bool stillExpiredRequest =
        connectionGattAuthorization_.pending &&
        connectionGattAuthorization_.generation == requestGeneration &&
        connectionGattAuthorization_.type == type;
    if (stillExpiredRequest) {
      clearCustomGattAuthorizationState();
    }
    bleExitCritical(timeoutIrqState);
    if (!stillExpiredRequest) {
      return BleGattAuthorizeReplyResult::kInvalidState;
    }
    emitBleTrace("ATT_AUTHORIZE_TIMEOUT");
    (void)disconnectWithReason(BleDisconnectReason::kInternalTerminate,
                               0x21U, 100000UL);
    return BleGattAuthorizeReplyResult::kTimeout;
  }

  uint8_t response[255] = {0};
  uint16_t responseLength = 0U;
  BleCustomCharacteristicState* commitCharacteristic = nullptr;
  uint8_t readCandidate[kCustomGattMaxValueLength] = {0};
  uint16_t readCandidateLength = 0U;
  bool commitReadUpdate = false;
  bool commitWrite = false;
  if (attError != 0U) {
    if (!buildAttErrorResponse(requestOpcode, requestHandle, attError,
                               response, &responseLength)) {
      return BleGattAuthorizeReplyResult::kInvalidState;
    }
  } else if (type == BleGattAuthorizeType::kRead) {
    BleCustomCharacteristicState* characteristic =
        findCustomCharacteristicByValueHandle(requestHandle);
    if (characteristic == nullptr ||
        (characteristic->properties & kBleGattPropRead) == 0U) {
      return BleGattAuthorizeReplyResult::kInvalidState;
    }

    readCandidateLength = characteristic->valueLength;
    if (readCandidateLength > 0U) {
      memcpy(readCandidate, characteristic->value, readCandidateLength);
    }
    if (update) {
      if (offset > characteristic->maxValueLength ||
          valueLength >
              static_cast<uint16_t>(characteristic->maxValueLength - offset)) {
        return BleGattAuthorizeReplyResult::kInvalidParameter;
      }
      if (characteristic->fixedValueLength && offset == 0U &&
          valueLength != characteristic->maxValueLength) {
        return BleGattAuthorizeReplyResult::kInvalidParameter;
      }
      if (valueLength > 0U) {
        memcpy(&readCandidate[offset], value, valueLength);
      }
      if (offset == 0U) {
        readCandidateLength = characteristic->fixedValueLength
                                  ? characteristic->maxValueLength
                                  : valueLength;
      } else if (static_cast<uint16_t>(offset + valueLength) >
                 readCandidateLength) {
        readCandidateLength = static_cast<uint16_t>(offset + valueLength);
      }
    }
    if (requestOffset > readCandidateLength) {
      return BleGattAuthorizeReplyResult::kInvalidParameter;
    }

    const uint16_t maxAttResponseLen = minU16(
        minU16(connectionAttMtu_, currentMaxAttPayloadLength()), 251U);
    if (maxAttResponseLen < 1U || maxAttResponseLen > sizeof(response)) {
      return BleGattAuthorizeReplyResult::kBusy;
    }
    const uint16_t available =
        static_cast<uint16_t>(readCandidateLength - requestOffset);
    const uint16_t toCopy =
        minU16(available, static_cast<uint16_t>(maxAttResponseLen - 1U));
    response[0] = (requestOpcode == kAttOpReadBlobReq)
                      ? kAttOpReadBlobRsp
                      : kAttOpReadRsp;
    if (toCopy > 0U) {
      memcpy(&response[1], &readCandidate[requestOffset], toCopy);
    }
    responseLength = static_cast<uint16_t>(1U + toCopy);
    commitCharacteristic = characteristic;
    commitReadUpdate = update;
  } else if (type == BleGattAuthorizeType::kWrite) {
    if (!update || offset != 0U) {
      return BleGattAuthorizeReplyResult::kInvalidParameter;
    }
    BleCustomCharacteristicState* characteristic =
        findCustomCharacteristicByValueHandle(requestHandle);
    if (characteristic == nullptr ||
        (characteristic->properties & kBleGattPropWrite) == 0U ||
        (characteristic->fixedValueLength
             ? (valueLength != characteristic->maxValueLength)
             : (valueLength > characteristic->maxValueLength))) {
      return BleGattAuthorizeReplyResult::kInvalidParameter;
    }
    uint8_t permissionError = kAttErrWriteNotPermitted;
    if (!customGattWritePermissionSatisfied(characteristic,
                                            &permissionError)) {
      return BleGattAuthorizeReplyResult::kInvalidParameter;
    }
    commitCharacteristic = characteristic;
    commitWrite = true;
    response[0] = kAttOpWriteRsp;
    responseLength = 1U;
  } else {
    return BleGattAuthorizeReplyResult::kInvalidParameter;
  }

  const uint32_t storeIrqState = bleEnterCritical();
  if (!connectionGattAuthorization_.pending ||
      connectionGattAuthorization_.handle != requestHandle ||
      connectionGattAuthorization_.type != type ||
      connectionGattAuthorization_.generation != requestGeneration ||
      !connected_ || connectionRole_ != BleConnectionRole::kPeripheral) {
    bleExitCritical(storeIrqState);
    return BleGattAuthorizeReplyResult::kInvalidState;
  }
  if (static_cast<int32_t>(millis() -
                           connectionGattAuthorization_.deadlineMs) >= 0) {
    clearCustomGattAuthorizationState();
    bleExitCritical(storeIrqState);
    emitBleTrace("ATT_AUTHORIZE_TIMEOUT");
    (void)disconnectWithReason(BleDisconnectReason::kInternalTerminate,
                               0x21U, 100000UL);
    return BleGattAuthorizeReplyResult::kTimeout;
  }

  if (commitWrite) {
    const BleCustomWriteHandlerState* handler =
        findCustomGattWriteHandler(requestHandle);
    const bool callbackNeeded =
        (handler != nullptr && handler->callback != nullptr) ||
        customGattWriteCallback_ != nullptr;
    const uint8_t deferredDepth = static_cast<uint8_t>(
        sizeof(deferredGattWrites_) / sizeof(deferredGattWrites_[0]));
    if (callbackNeeded && deferredGattWriteCount_ >= deferredDepth) {
      bleExitCritical(storeIrqState);
      return BleGattAuthorizeReplyResult::kBusy;
    }
    uint8_t writeError = kAttErrWriteNotPermitted;
    if (!applyCustomGattCharacteristicValueWrite(
            requestHandle, value, valueLength, true, &writeError, true)) {
      bleExitCritical(storeIrqState);
      return BleGattAuthorizeReplyResult::kInvalidParameter;
    }
  } else if (commitReadUpdate) {
    commitCharacteristic->valueLength = readCandidateLength;
    memset(commitCharacteristic->value, 0,
           sizeof(commitCharacteristic->value));
    if (readCandidateLength > 0U) {
      memcpy(commitCharacteristic->value, readCandidate,
             readCandidateLength);
    }
  }
  memcpy(connectionGattAuthorization_.response, response, responseLength);
  connectionGattAuthorization_.responseLength = responseLength;
  connectionGattAuthorization_.pending = false;
  connectionGattAuthorization_.callbackPending = false;
  connectionGattAuthorization_.responseReady = true;
  bleExitCritical(storeIrqState);
  emitBleTrace(attError == 0U ? "ATT_AUTHORIZE_ACCEPT"
                              : "ATT_AUTHORIZE_REJECT");
  return BleGattAuthorizeReplyResult::kSuccess;
}

bool BleRadio::takeCustomGattAuthorizationResponse(
    uint8_t* outL2capPayload, uint8_t* outPayloadLength) {
  if (outL2capPayload == nullptr || outPayloadLength == nullptr) {
    return false;
  }
  const uint32_t irqState = bleEnterCritical();
  if (!connectionGattAuthorization_.responseReady ||
      connectionGattAuthorization_.responseLength == 0U ||
      connectionGattAuthorization_.responseLength > connectionAttMtu_) {
    bleExitCritical(irqState);
    return false;
  }
  const uint16_t totalLength = static_cast<uint16_t>(
      kBleL2capHeaderLen + connectionGattAuthorization_.responseLength);
  if (totalLength > 255U || totalLength > currentTxDataPduPayloadLength()) {
    bleExitCritical(irqState);
    return false;
  }
  writeLe16(&outL2capPayload[0],
            connectionGattAuthorization_.responseLength);
  writeLe16(&outL2capPayload[2], kBleL2capCidAtt);
  memcpy(&outL2capPayload[kBleL2capHeaderLen],
         connectionGattAuthorization_.response,
         connectionGattAuthorization_.responseLength);
  *outPayloadLength = static_cast<uint8_t>(totalLength);
  clearCustomGattAuthorizationState();
  bleExitCritical(irqState);
  return true;
}

void BleRadio::serviceCustomGattAuthorizationTimeout() {
  const uint32_t irqState = bleEnterCritical();
  const bool expired =
      (connectionGattAuthorization_.pending ||
       connectionGattAuthorization_.responseReady) &&
      static_cast<int32_t>(millis() -
                           connectionGattAuthorization_.deadlineMs) >= 0;
  if (!expired) {
    bleExitCritical(irqState);
    return;
  }
  clearCustomGattAuthorizationState();
  bleExitCritical(irqState);
  emitBleTrace("ATT_AUTHORIZE_TIMEOUT");
  if (connected_) {
    (void)disconnectWithReason(BleDisconnectReason::kInternalTerminate,
                               0x21U, 100000UL);
  }
}

void BleRadio::clearIncomingL2capReassembly() {
  connectionRxL2capReassemblyActive_ = false;
  connectionRxL2capReassemblyExpectedLength_ = 0U;
  connectionRxL2capReassemblyReceivedLength_ = 0U;
  memset(connectionRxL2capReassemblyBuffer_, 0,
         sizeof(connectionRxL2capReassemblyBuffer_));
}

bool BleRadio::assembleIncomingL2capPayload(uint8_t llid,
                                            const uint8_t* l2capPayload,
                                            uint8_t l2capPayloadLength,
                                            bool packetIsNew,
                                            const uint8_t** outPayload,
                                            uint8_t* outPayloadLength) {
  if (l2capPayload == nullptr || outPayload == nullptr ||
      outPayloadLength == nullptr || l2capPayloadLength == 0U) {
    return false;
  }

  *outPayload = nullptr;
  *outPayloadLength = 0U;

  if (llid == kBlePduDataStartOrComplete) {
    if (l2capPayloadLength < kBleL2capHeaderLen) {
      clearIncomingL2capReassembly();
      return false;
    }

    const uint16_t declaredLength = readLe16(&l2capPayload[0]);
    const uint16_t totalLength =
        static_cast<uint16_t>(kBleL2capHeaderLen + declaredLength);

    if (connectionRxL2capReassemblyActive_) {
      const uint16_t activeReceived =
          connectionRxL2capReassemblyReceivedLength_;
      const uint16_t activeExpected =
          connectionRxL2capReassemblyExpectedLength_;
      if (totalLength == activeExpected &&
          activeReceived >= l2capPayloadLength &&
          memcmp(connectionRxL2capReassemblyBuffer_, l2capPayload,
                 l2capPayloadLength) == 0) {
        emitBleTrace("L2CAP_FRAG_DUP_START");
        return false;
      }
      clearIncomingL2capReassembly();
    }

    if (totalLength <= l2capPayloadLength) {
      *outPayload = l2capPayload;
      *outPayloadLength = static_cast<uint8_t>(totalLength);
      return true;
    }

    if (!packetIsNew || totalLength > sizeof(connectionRxL2capReassemblyBuffer_)) {
      return false;
    }

    memcpy(connectionRxL2capReassemblyBuffer_, l2capPayload, l2capPayloadLength);
    connectionRxL2capReassemblyActive_ = true;
    connectionRxL2capReassemblyExpectedLength_ = totalLength;
    connectionRxL2capReassemblyReceivedLength_ = l2capPayloadLength;
    emitBleTrace("L2CAP_FRAG_START");
    return false;
  }

  if (llid != 0x01U || !connectionRxL2capReassemblyActive_) {
    return false;
  }

  if (!packetIsNew) {
    return false;
  }

  const uint16_t received = connectionRxL2capReassemblyReceivedLength_;
  const uint16_t expected = connectionRxL2capReassemblyExpectedLength_;
  if ((received >= expected) || (received < kBleL2capHeaderLen)) {
    clearIncomingL2capReassembly();
    return false;
  }

  const uint16_t remaining = static_cast<uint16_t>(expected - received);
  if (l2capPayloadLength > remaining) {
    // LL payload length is exact. A continuation longer than the remaining
    // L2CAP SDU cannot be the final fragment; it is usually the same RX packet
    // being observed again by a later event path. Never partial-copy it,
    // otherwise the tail of SMP public keys can be replaced with duplicate data.
    if (received >= l2capPayloadLength &&
        memcmp(&connectionRxL2capReassemblyBuffer_[received - l2capPayloadLength],
               l2capPayload, l2capPayloadLength) == 0) {
      emitBleTrace("L2CAP_FRAG_DUP_CONT");
      return false;
    }
    emitBleTrace("L2CAP_FRAG_OVERSIZE_CONT");
    clearIncomingL2capReassembly();
    return false;
  }

  memcpy(&connectionRxL2capReassemblyBuffer_[received], l2capPayload,
         l2capPayloadLength);
  connectionRxL2capReassemblyReceivedLength_ =
      static_cast<uint16_t>(received + l2capPayloadLength);
  emitBleTrace("L2CAP_FRAG_CONT");
  if (connectionRxL2capReassemblyReceivedLength_ < expected) {
    return false;
  }

  *outPayload = connectionRxL2capReassemblyBuffer_;
  *outPayloadLength = static_cast<uint8_t>(expected);
  connectionRxL2capReassemblyActive_ = false;
  connectionRxL2capReassemblyExpectedLength_ = 0U;
  connectionRxL2capReassemblyReceivedLength_ = 0U;
  emitBleTrace("L2CAP_FRAG_DONE");
  return true;
}

bool BleRadio::applyCccdState(uint16_t handle, uint16_t cccd) {
  if (handle == kHandleGattServiceChangedCccd) {
    if ((cccd & ~0x0002U) != 0U) {
      return false;
    }
    const bool enableIndication = ((cccd & 0x0002U) != 0U);
    if (!enableIndication) {
      connectionServiceChangedIndicationPending_ = false;
      connectionServiceChangedIndicationAwaitingConfirm_ = false;
      connectionServiceChangedIndicationStartHandle_ = 0U;
      connectionServiceChangedIndicationEndHandle_ = 0U;
      connectionServiceChangedIndicationGeneration_ = 0U;
    }
    connectionServiceChangedIndicationsEnabled_ = enableIndication;
    if (enableIndication) {
      scheduleBondedServiceChangedIfReady();
    }
    return true;
  }
  if (handle == kHandleBatteryLevelCccd) {
    if ((cccd & ~0x0001U) != 0U) {
      return false;
    }
    const bool enableNotify = ((cccd & 0x0001U) != 0U);
    connectionBatteryNotificationsEnabled_ = enableNotify;
    connectionBatteryNotificationPending_ = enableNotify;
    return true;
  }

  BleCustomCharacteristicState* custom = findCustomCharacteristicByCccdHandle(handle);
  if (custom == nullptr) {
    return false;
  }

  uint16_t allowedMask = 0U;
  if ((custom->properties & kBleGattPropNotify) != 0U) {
    allowedMask |= 0x0001U;
  }
  if ((custom->properties & kBleGattPropIndicate) != 0U) {
    allowedMask |= 0x0002U;
  }
  if (allowedMask == 0U || ((cccd & ~allowedMask) != 0U)) {
    return false;
  }

  custom->cccdValue = cccd;
  if (((custom->cccdValue & 0x0002U) == 0U) &&
      (connectionCustomIndicationAwaitingHandle_ == custom->valueHandle)) {
    connectionCustomIndicationAwaitingHandle_ = 0U;
  }
  const ptrdiff_t idx = (custom - &customGattCharacteristics_[0]);
  if (idx >= 0 &&
      static_cast<uint8_t>(idx) < customGattCharacteristicCount_) {
    filterCustomGattNotificationQueue(static_cast<uint8_t>(idx),
                                      (custom->cccdValue & 0x0001U) != 0U,
                                      (custom->cccdValue & 0x0002U) != 0U);
  }
  return true;
}

bool BleRadio::rebuildGattDatabaseHash() {
  const uint8_t zeroKey[16] = {0};
  AesCmacStream cmac;
  if (!cmac.begin(zeroKey)) {
    gattDatabaseHashValid_ = false;
    return false;
  }

  auto append16 = [&cmac](uint16_t value) {
    uint8_t bytes[2] = {static_cast<uint8_t>(value & 0xFFU),
                        static_cast<uint8_t>((value >> 8U) & 0xFFU)};
    return cmac.update(bytes, sizeof(bytes));
  };
  auto appendService = [&cmac, &append16](uint16_t handle,
                                          const uint8_t* uuid,
                                          uint8_t uuidLength) {
    return append16(handle) && append16(kUuidPrimaryService) &&
           cmac.update(uuid, uuidLength);
  };
  auto appendCharacteristic = [&cmac, &append16](
                                  uint16_t declarationHandle,
                                  uint8_t properties, uint16_t valueHandle,
                                  const uint8_t* uuid, uint8_t uuidLength) {
    return append16(declarationHandle) && append16(kUuidCharacteristic) &&
           cmac.update(&properties, 1U) && append16(valueHandle) &&
           cmac.update(uuid, uuidLength);
  };
  auto appendDescriptor = [&append16](uint16_t handle, uint16_t uuid) {
    return append16(handle) && append16(uuid);
  };

  for (uint16_t handle = kHandleGapService;
       handle <= kHandleBatteryLevelCccd; ++handle) {
    bool appended = false;
    for (const BlePrimaryServiceRecord& service : kBlePrimaryServices) {
      if (service.startHandle == handle) {
        uint8_t uuid[2] = {static_cast<uint8_t>(service.uuid16 & 0xFFU),
                           static_cast<uint8_t>(service.uuid16 >> 8U)};
        appended = appendService(handle, uuid, sizeof(uuid));
        break;
      }
    }
    if (!appended) {
      for (const BleCharacteristicRecord& characteristic : kBleCharacteristics) {
        if (characteristic.declarationHandle == handle) {
          uint8_t uuid[2] = {
              static_cast<uint8_t>(characteristic.uuid16 & 0xFFU),
              static_cast<uint8_t>(characteristic.uuid16 >> 8U)};
          appended = appendCharacteristic(
              handle, characteristic.properties, characteristic.valueHandle,
              uuid, sizeof(uuid));
          break;
        }
      }
    }
    if (!appended && (handle == kHandleGattServiceChangedCccd ||
                      handle == kHandleBatteryLevelCccd)) {
      appended = appendDescriptor(handle, kUuidClientCharacteristicConfig);
    }
    if (!appended &&
        (handle == kHandleGapService || handle == kHandleGattService ||
         handle == kHandleBatteryService ||
         handle == kHandleGapDeviceNameDecl ||
         handle == kHandleGapAppearanceDecl || handle == kHandleGapPpcpDecl ||
         handle == kHandleGattServiceChangedDecl ||
         handle == kHandleGattClientFeaturesDecl ||
         handle == kHandleGattDatabaseHashDecl ||
         handle == kHandleBatteryLevelDecl ||
         handle == kHandleGattServiceChangedCccd ||
         handle == kHandleBatteryLevelCccd)) {
      gattDatabaseHashValid_ = false;
      return false;
    }
  }

  for (uint16_t handle = kCustomGattHandleStart;
       handle < customGattNextHandle_; ++handle) {
    bool appended = false;
    for (uint8_t i = 0U; i < customGattServiceCount_; ++i) {
      const BleCustomServiceState& service = customGattServices_[i];
      if (service.serviceHandle == handle) {
        appended = appendService(handle, service.uuid, service.uuidSize);
        break;
      }
    }
    if (appended) {
      continue;
    }
    for (uint8_t i = 0U; i < customGattCharacteristicCount_; ++i) {
      const BleCustomCharacteristicState& characteristic =
          customGattCharacteristics_[i];
      if (characteristic.declarationHandle == handle) {
        appended = appendCharacteristic(
            handle, characteristic.properties, characteristic.valueHandle,
            characteristic.uuid, characteristic.uuidSize);
      } else if (characteristic.cccdHandle == handle) {
        appended = appendDescriptor(handle, kUuidClientCharacteristicConfig);
      } else if (characteristic.userDescriptionHandle == handle) {
        appended = appendDescriptor(handle,
                                    kUuidCharacteristicUserDescription);
      } else if (characteristic.presentationFormatHandle == handle) {
        appended = appendDescriptor(
            handle, kUuidCharacteristicPresentationFormat);
      } else if (characteristic.valueHandle == handle ||
                 characteristic.reportReferenceHandle == handle) {
        appended = true;  // These attribute types are excluded from the hash.
      }
      if (appended) {
        break;
      }
    }
    if (!appended) {
      gattDatabaseHashValid_ = false;
      return false;
    }
  }

  gattDatabaseHashValid_ = cmac.finish(gattDatabaseHash_);
  return gattDatabaseHashValid_;
}

bool BleRadio::getGattDatabaseHash(uint8_t outHash[16]) const {
  if (outHash == nullptr || !gattDatabaseHashValid_) {
    return false;
  }
  memcpy(outHash, gattDatabaseHash_, sizeof(gattDatabaseHash_));
  return true;
}

uint8_t BleRadio::gattClientSupportedFeatures() const {
  return connectionGattClientSupportedFeatures_;
}

bool BleRadio::gattClientChangeAware() const {
  return connectionGattClientChangeAware_;
}

uint8_t BleRadio::readAttributeValue(uint16_t handle, uint16_t offset,
                                     uint8_t* outValue, uint8_t maxLen) const {
  if (outValue == nullptr) {
    return kAttrReadInvalidHandleLen;
  }

  const BleCustomCharacteristicState* directCharacteristic =
      findCustomCharacteristicByValueHandle(handle);
  if (directCharacteristic != nullptr) {
    if ((directCharacteristic->properties & kBleGattPropRead) == 0U) {
      return kAttrReadNotPermittedLen;
    }
    uint8_t permissionError = kAttErrReadNotPermitted;
    if (!customGattReadPermissionSatisfied(directCharacteristic, &permissionError)) {
      if (permissionError == kAttErrInsufficientEncryption) {
        return kAttrReadInsufficientEncryptionLen;
      }
      if (permissionError == kAttErrInsufficientAuthentication) {
        return kAttrReadInsufficientAuthenticationLen;
      }
      return kAttrReadNotPermittedLen;
    }
    if (offset > directCharacteristic->valueLength) {
      return kAttrReadInvalidOffsetLen;
    }
    const uint16_t remaining =
        static_cast<uint16_t>(directCharacteristic->valueLength - offset);
    const uint8_t toCopy =
        static_cast<uint8_t>(min<uint16_t>(remaining, maxLen));
    if (toCopy > 0U) {
      memcpy(outValue, &directCharacteristic->value[offset], toCopy);
    }
    return toCopy;
  }

  const BleCustomCharacteristicState* directUserDescription =
      findCustomCharacteristicByUserDescriptionHandle(handle);
  if (directUserDescription != nullptr) {
    uint8_t permissionError = kAttErrReadNotPermitted;
    if (!customGattReadPermissionSatisfied(directUserDescription,
                                           &permissionError)) {
      return attPermissionErrorToReadSentinel(permissionError);
    }
    if (offset > directUserDescription->userDescriptionLength) {
      return kAttrReadInvalidOffsetLen;
    }
    const uint8_t remaining =
        static_cast<uint8_t>(directUserDescription->userDescriptionLength - offset);
    const uint8_t toCopy = minU8(remaining, maxLen);
    if (toCopy > 0U) {
      memcpy(outValue, &directUserDescription->userDescription[offset], toCopy);
    }
    return toCopy;
  }

  const BleCustomCharacteristicState* directPresentationFormat =
      findCustomCharacteristicByPresentationFormatHandle(handle);
  if (directPresentationFormat != nullptr) {
    uint8_t permissionError = kAttErrReadNotPermitted;
    if (!customGattReadPermissionSatisfied(directPresentationFormat,
                                           &permissionError)) {
      return attPermissionErrorToReadSentinel(permissionError);
    }
    if (offset > kCustomGattPresentationFormatLength) {
      return kAttrReadInvalidOffsetLen;
    }
    const uint8_t remaining =
        static_cast<uint8_t>(kCustomGattPresentationFormatLength - offset);
    const uint8_t toCopy = minU8(remaining, maxLen);
    if (toCopy > 0U) {
      memcpy(outValue, &directPresentationFormat->presentationFormat[offset],
             toCopy);
    }
    return toCopy;
  }

  const BleCustomCharacteristicState* directReportReference =
      findCustomCharacteristicByReportReferenceHandle(handle);
  if (directReportReference != nullptr) {
    uint8_t permissionError = kAttErrReadNotPermitted;
    if (!customGattPermissionSatisfied(
            directReportReference->reportReferenceReadPermission,
            kAttErrReadNotPermitted, &permissionError)) {
      return attPermissionErrorToReadSentinel(permissionError);
    }
    if (offset > kCustomGattReportReferenceLength) {
      return kAttrReadInvalidOffsetLen;
    }
    const uint8_t remaining =
        static_cast<uint8_t>(kCustomGattReportReferenceLength - offset);
    const uint8_t toCopy = minU8(remaining, maxLen);
    if (toCopy > 0U) {
      memcpy(outValue, &directReportReference->reportReference[offset],
             toCopy);
    }
    return toCopy;
  }

  uint8_t fullValue[31] = {0};
  uint8_t fullLen = 0U;

  switch (handle) {
    case kHandleGapService:
      writeLe16(&fullValue[0], kUuidGapService);
      fullLen = 2U;
      break;
    case kHandleGapDeviceNameDecl:
      fullValue[0] = kGattCharacteristicPropRead;
      writeLe16(&fullValue[1], kHandleGapDeviceNameValue);
      writeLe16(&fullValue[3], kUuidDeviceName);
      fullLen = 5U;
      break;
    case kHandleGapDeviceNameValue:
      if (gapDeviceNameLen_ > 0U) {
        memcpy(fullValue, gapDeviceName_, gapDeviceNameLen_);
      }
      fullLen = gapDeviceNameLen_;
      break;
    case kHandleGapAppearanceDecl:
      fullValue[0] = kGattCharacteristicPropRead;
      writeLe16(&fullValue[1], kHandleGapAppearanceValue);
      writeLe16(&fullValue[3], kUuidAppearance);
      fullLen = 5U;
      break;
    case kHandleGapAppearanceValue:
      writeLe16(&fullValue[0], gapAppearance_);
      fullLen = 2U;
      break;
    case kHandleGapPpcpDecl:
      fullValue[0] = kGattCharacteristicPropRead;
      writeLe16(&fullValue[1], kHandleGapPpcpValue);
      writeLe16(&fullValue[3], kUuidPpcp);
      fullLen = 5U;
      break;
    case kHandleGapPpcpValue:
      writeLe16(&fullValue[0], gapPpcpIntervalMin_);
      writeLe16(&fullValue[2], gapPpcpIntervalMax_);
      writeLe16(&fullValue[4], gapPpcpLatency_);
      writeLe16(&fullValue[6], gapPpcpTimeout_);
      fullLen = 8U;
      break;
    case kHandleGattService:
      writeLe16(&fullValue[0], kUuidGattService);
      fullLen = 2U;
      break;
    case kHandleGattServiceChangedDecl:
      fullValue[0] = kGattCharacteristicPropIndicate;
      writeLe16(&fullValue[1], kHandleGattServiceChangedValue);
      writeLe16(&fullValue[3], kUuidServiceChanged);
      fullLen = 5U;
      break;
    case kHandleGattServiceChangedValue:
      writeLe16(&fullValue[0], 0x0001U);
      writeLe16(&fullValue[2], 0xFFFFU);
      fullLen = 4U;
      break;
    case kHandleGattServiceChangedCccd:
      writeLe16(&fullValue[0],
                connectionServiceChangedIndicationsEnabled_ ? 0x0002U : 0x0000U);
      fullLen = 2U;
      break;
    case kHandleGattClientFeaturesDecl:
      fullValue[0] = static_cast<uint8_t>(kGattCharacteristicPropRead |
                                          kGattCharacteristicPropWrite);
      writeLe16(&fullValue[1], kHandleGattClientFeaturesValue);
      writeLe16(&fullValue[3], kUuidClientSupportedFeatures);
      fullLen = 5U;
      break;
    case kHandleGattClientFeaturesValue:
      fullValue[0] = connectionGattClientSupportedFeatures_;
      fullLen = 1U;
      break;
    case kHandleGattDatabaseHashDecl:
      fullValue[0] = kGattCharacteristicPropRead;
      writeLe16(&fullValue[1], kHandleGattDatabaseHashValue);
      writeLe16(&fullValue[3], kUuidDatabaseHash);
      fullLen = 5U;
      break;
    case kHandleGattDatabaseHashValue:
      if (!gattDatabaseHashValid_) {
        return kAttrReadInvalidHandleLen;
      }
      memcpy(fullValue, gattDatabaseHash_, sizeof(gattDatabaseHash_));
      fullLen = sizeof(gattDatabaseHash_);
      break;
    case kHandleBatteryService:
      writeLe16(&fullValue[0], kUuidBatteryService);
      fullLen = 2U;
      break;
    case kHandleBatteryLevelDecl:
      fullValue[0] = static_cast<uint8_t>(kGattCharacteristicPropRead |
                                          kGattCharacteristicPropNotify);
      writeLe16(&fullValue[1], kHandleBatteryLevelValue);
      writeLe16(&fullValue[3], kUuidBatteryLevel);
      fullLen = 5U;
      break;
    case kHandleBatteryLevelValue:
      fullValue[0] = gapBatteryLevel_;
      fullLen = 1U;
      break;
    case kHandleBatteryLevelCccd:
      writeLe16(&fullValue[0], connectionBatteryNotificationsEnabled_ ? 0x0001U : 0x0000U);
      fullLen = 2U;
      break;
    default: {
      const BleCustomServiceState* service = findCustomServiceByHandle(handle);
      if (service != nullptr) {
        if (service->uuidSize == kBleUuid128Size) {
          memcpy(&fullValue[0], service->uuid, kBleUuid128Size);
          fullLen = kBleUuid128Size;
        } else {
          writeLe16(&fullValue[0], bleUuidRead16(service->uuid, service->uuidSize));
          fullLen = kBleUuid16Size;
        }
        break;
      }

      const BleCustomCharacteristicState* characteristic =
          findCustomCharacteristicByCccdHandle(handle);
      if (characteristic != nullptr) {
        writeLe16(&fullValue[0], characteristic->cccdValue);
        fullLen = 2U;
        break;
      }

      for (uint8_t i = 0U; i < customGattCharacteristicCount_; ++i) {
        const BleCustomCharacteristicState& candidate = customGattCharacteristics_[i];
        if (candidate.declarationHandle != handle) {
          continue;
        }
        writeCharacteristicDeclarationValue(fullValue, candidate.properties,
                                            candidate.valueHandle, candidate.uuid,
                                            candidate.uuidSize);
        fullLen = bleCharacteristicDeclarationValueLen(candidate.uuidSize);
        break;
      }
      if (fullLen == 0U) {
        return kAttrReadInvalidHandleLen;
      }
      break;
    }
  }

  if (offset > fullLen) {
    return kAttrReadInvalidOffsetLen;
  }
  const uint8_t remaining = static_cast<uint8_t>(fullLen - offset);
  const uint8_t toCopy = minU8(remaining, maxLen);
  if (toCopy > 0U) {
    memcpy(outValue, &fullValue[offset], toCopy);
  }
  return toCopy;
}

bool BleRadio::buildAttResponse(const uint8_t* attRequest, uint16_t requestLength,
                                uint8_t* outAttResponse,
                                uint16_t* outAttResponseLength) {
  if (attRequest == nullptr || outAttResponse == nullptr ||
      outAttResponseLength == nullptr || requestLength == 0U) {
    return false;
  }

  const uint16_t maxAttResponseLen =
      minU16(connectionAttMtu_, currentMaxAttPayloadLength());
  if (maxAttResponseLen < 1U) {
    return false;
  }
  const uint8_t maxAttValueLen =
      static_cast<uint8_t>((maxAttResponseLen > 1U) ? (maxAttResponseLen - 1U) : 0U);

  const uint8_t opcode = attRequest[0];
  *outAttResponseLength = 0U;

  const auto emitAttUuidTrace = [this](uint16_t uuid) {
    const auto emitHexNibbleTrace = [this](uint8_t nibble) {
      switch (nibble & 0x0FU) {
        case 0x0U:
          emitBleTrace("ATT_HEX_0");
          break;
        case 0x1U:
          emitBleTrace("ATT_HEX_1");
          break;
        case 0x2U:
          emitBleTrace("ATT_HEX_2");
          break;
        case 0x3U:
          emitBleTrace("ATT_HEX_3");
          break;
        case 0x4U:
          emitBleTrace("ATT_HEX_4");
          break;
        case 0x5U:
          emitBleTrace("ATT_HEX_5");
          break;
        case 0x6U:
          emitBleTrace("ATT_HEX_6");
          break;
        case 0x7U:
          emitBleTrace("ATT_HEX_7");
          break;
        case 0x8U:
          emitBleTrace("ATT_HEX_8");
          break;
        case 0x9U:
          emitBleTrace("ATT_HEX_9");
          break;
        case 0xAU:
          emitBleTrace("ATT_HEX_A");
          break;
        case 0xBU:
          emitBleTrace("ATT_HEX_B");
          break;
        case 0xCU:
          emitBleTrace("ATT_HEX_C");
          break;
        case 0xDU:
          emitBleTrace("ATT_HEX_D");
          break;
        case 0xEU:
          emitBleTrace("ATT_HEX_E");
          break;
        default:
          emitBleTrace("ATT_HEX_F");
          break;
      }
    };

    switch (uuid) {
      case kUuidPrimaryService:
        emitBleTrace("ATT_RX_UUID_PRIMARY");
        break;
      case kUuidCharacteristic:
        emitBleTrace("ATT_RX_UUID_CHAR");
        break;
      case kUuidClientCharacteristicConfig:
        emitBleTrace("ATT_RX_UUID_CCCD");
        break;
      case kUuidReportReference:
        emitBleTrace("ATT_RX_UUID_REPORT_REF");
        break;
      case kUuidDeviceName:
        emitBleTrace("ATT_RX_UUID_NAME");
        break;
      case kUuidAppearance:
        emitBleTrace("ATT_RX_UUID_APPEAR");
        break;
      case kUuidPpcp:
        emitBleTrace("ATT_RX_UUID_PPCP");
        break;
      case kUuidServiceChanged:
        emitBleTrace("ATT_RX_UUID_SVC_CHANGED");
        break;
      case kUuidBatteryLevel:
        emitBleTrace("ATT_RX_UUID_BATT");
        break;
      case 0x2A22U:
        emitBleTrace("ATT_RX_UUID_BOOT_KBD_IN");
        break;
      case 0x2A23U:
        emitBleTrace("ATT_RX_UUID_SYSTEM_ID");
        break;
      case 0x2A24U:
        emitBleTrace("ATT_RX_UUID_MODEL");
        break;
      case 0x2A25U:
        emitBleTrace("ATT_RX_UUID_SERIAL");
        break;
      case 0x2A26U:
        emitBleTrace("ATT_RX_UUID_FW_REV");
        break;
      case 0x2A27U:
        emitBleTrace("ATT_RX_UUID_HW_REV");
        break;
      case 0x2A28U:
        emitBleTrace("ATT_RX_UUID_SW_REV");
        break;
      case 0x2A29U:
        emitBleTrace("ATT_RX_UUID_MANUF");
        break;
      case 0x2A2AU:
        emitBleTrace("ATT_RX_UUID_REG_CERT");
        break;
      case 0x2A32U:
        emitBleTrace("ATT_RX_UUID_BOOT_KBD_OUT");
        break;
      case 0x2A33U:
        emitBleTrace("ATT_RX_UUID_BOOT_MOUSE_IN");
        break;
      case 0x2A4AU:
        emitBleTrace("ATT_RX_UUID_HID_INFO");
        break;
      case 0x2A4BU:
        emitBleTrace("ATT_RX_UUID_REPORT_MAP");
        break;
      case 0x2A4CU:
        emitBleTrace("ATT_RX_UUID_HID_CTRL");
        break;
      case 0x2A4DU:
        emitBleTrace("ATT_RX_UUID_REPORT");
        break;
      case 0x2A4EU:
        emitBleTrace("ATT_RX_UUID_PROTOCOL");
        break;
      case 0x2A50U:
        emitBleTrace("ATT_RX_UUID_PNP_ID");
        break;
      case 0x2AA6U:
        emitBleTrace("ATT_RX_UUID_CENT_ADDR_RES");
        break;
      case 0x2AC9U:
        emitBleTrace("ATT_RX_UUID_RPA_ONLY");
        break;
      default:
        emitBleTrace("ATT_RX_UUID_OTHER");
        emitBleTrace("ATT_RX_UUID_HEX");
        emitHexNibbleTrace(static_cast<uint8_t>((uuid >> 12U) & 0x0FU));
        emitHexNibbleTrace(static_cast<uint8_t>((uuid >> 8U) & 0x0FU));
        emitHexNibbleTrace(static_cast<uint8_t>((uuid >> 4U) & 0x0FU));
        emitHexNibbleTrace(static_cast<uint8_t>(uuid & 0x0FU));
        break;
    }
  };

  const auto emitAttServiceTrace = [this](uint16_t uuid) {
    switch (uuid) {
      case kUuidGapService:
        emitBleTrace("ATT_RX_SVC_GAP");
        break;
      case kUuidGattService:
        emitBleTrace("ATT_RX_SVC_GATT");
        break;
      case kUuidBatteryService:
        emitBleTrace("ATT_RX_SVC_BATT");
        break;
      case 0x180AU:
        emitBleTrace("ATT_RX_SVC_DIS");
        break;
      case 0x1812U:
        emitBleTrace("ATT_RX_SVC_HID");
        break;
      default:
        emitBleTrace("ATT_RX_SVC_OTHER");
        break;
    }
  };

  const auto emitAttHandleTrace = [this, &emitAttUuidTrace](uint16_t handle) {
    if (handle >= kHandleGapService && handle <= kHandleGapPpcpValue) {
      emitBleTrace("ATT_RX_HANDLE_GAP");
      return;
    }
    if (handle >= kHandleGattService &&
        handle <= kHandleGattDatabaseHashValue) {
      emitBleTrace("ATT_RX_HANDLE_GATT");
      return;
    }
    if (handle >= kHandleBatteryService && handle <= kHandleBatteryLevelCccd) {
      emitBleTrace("ATT_RX_HANDLE_BATT");
      return;
    }
    for (uint8_t i = 0U; i < customGattCharacteristicCount_; ++i) {
      const BleCustomCharacteristicState& ch = customGattCharacteristics_[i];
      if (handle == ch.declarationHandle) {
        emitBleTrace("ATT_RX_HANDLE_CUSTOM_DECL");
        emitAttUuidTrace(bleUuidRead16(ch.uuid, ch.uuidSize));
        return;
      }
      if (handle == ch.valueHandle) {
        emitBleTrace("ATT_RX_HANDLE_CUSTOM_VALUE");
        emitAttUuidTrace(bleUuidRead16(ch.uuid, ch.uuidSize));
        return;
      }
      if (handle == ch.userDescriptionHandle) {
        emitBleTrace("ATT_RX_HANDLE_CUSTOM_USERDESC");
        return;
      }
      if (handle == ch.presentationFormatHandle) {
        emitBleTrace("ATT_RX_HANDLE_CUSTOM_FORMAT");
        return;
      }
      if (handle == ch.reportReferenceHandle) {
        emitBleTrace("ATT_RX_HANDLE_CUSTOM_REPORTREF");
        return;
      }
      if (handle == ch.cccdHandle) {
        emitBleTrace("ATT_RX_HANDLE_CUSTOM_CCCD");
        emitAttUuidTrace(bleUuidRead16(ch.uuid, ch.uuidSize));
        return;
      }
    }
    emitBleTrace("ATT_RX_HANDLE_OTHER");
  };

  const bool robustCachingClient =
      (connectionGattClientSupportedFeatures_ & 0x01U) != 0U;
  const bool isAttCommand =
      opcode == kAttOpWriteCmd || opcode == kAttOpSignedWriteCmd;
  const bool isAttRequest =
      opcode == kAttOpExchangeMtuReq || opcode == kAttOpFindInfoReq ||
      opcode == kAttOpFindByTypeValueReq || opcode == kAttOpReadByTypeReq ||
      opcode == kAttOpReadReq || opcode == kAttOpReadBlobReq ||
      opcode == kAttOpReadMultipleReq ||
      opcode == kAttOpReadMultipleVariableReq ||
      opcode == kAttOpReadByGroupTypeReq || opcode == kAttOpWriteReq ||
      opcode == kAttOpPrepareWriteReq || opcode == kAttOpExecuteWriteReq;
  if (robustCachingClient && !connectionGattClientChangeAware_) {
    if (isAttRequest &&
        (connectionGattOutOfSyncSent_ || connectionGattDatabaseHashRead_)) {
      setGattClientChangeAware(true, true);
    } else if (isAttCommand) {
      emitBleTrace("GATT_ROBUST_CACHE_COMMAND_IGNORED");
      return false;
    } else if (isAttRequest) {
      bool requiresOutOfSync =
          opcode == kAttOpReadReq || opcode == kAttOpReadBlobReq ||
          opcode == kAttOpReadMultipleReq ||
          opcode == kAttOpReadMultipleVariableReq || opcode == kAttOpWriteReq ||
          opcode == kAttOpPrepareWriteReq || opcode == kAttOpExecuteWriteReq;
      if (opcode == kAttOpReadByTypeReq && requestLength >= 7U) {
        const uint16_t startHandle = readLe16(&attRequest[1]);
        const uint16_t endHandle = readLe16(&attRequest[3]);
        const uint16_t attributeType =
            requestLength == 7U ? readLe16(&attRequest[5]) : 0U;
        const bool discoveryType = attributeType == kUuidInclude ||
                                   attributeType == kUuidCharacteristic;
        const bool fullRange = startHandle == 0x0001U &&
                               endHandle == 0xFFFFU;
        requiresOutOfSync = !discoveryType && !fullRange;
      }
      if (requiresOutOfSync) {
        connectionGattOutOfSyncSent_ = true;
        emitBleTrace("GATT_DATABASE_OUT_OF_SYNC");
        const uint16_t handle = requestLength >= 3U
                                    ? readLe16(&attRequest[1])
                                    : 0U;
        return buildAttErrorResponse(opcode, handle,
                                     kAttErrDatabaseOutOfSync,
                                     outAttResponse, outAttResponseLength);
      }
    }
  }

  switch (opcode) {
    case kAttOpExchangeMtuReq: {
      emitBleTrace("ATT_RX_MTU_REQ");
      if (requestLength < 3U) {
        return buildAttErrorResponse(opcode, 0U, kAttErrInvalidPdu, outAttResponse,
                                     outAttResponseLength);
      }

      const uint16_t clientRxMtu = readLe16(&attRequest[1]);
      uint16_t serverRxMtu = connectionRequestedAttMtu_;
      if (serverRxMtu < kBleDefaultAttMtu) {
        serverRxMtu = kBleDefaultAttMtu;
      } else if (serverRxMtu > localPreferredAttMtu()) {
        serverRxMtu = localPreferredAttMtu();
      }
      const uint16_t negotiatedMtu = minU16(clientRxMtu, serverRxMtu);
      connectionAttMtu_ = (negotiatedMtu >= 23U) ? negotiatedMtu : 23U;
      connectionAttMtuExchangePending_ = false;
      connectionAttMtuExchangeInFlight_ = false;
      connectionAttMtuExchangeComplete_ = true;

      outAttResponse[0] = kAttOpExchangeMtuRsp;
      writeLe16(&outAttResponse[1], serverRxMtu);
      *outAttResponseLength = 3U;
      return true;
    }

    case kAttOpFindInfoReq: {
      emitBleTrace("ATT_RX_FIND_INFO_REQ");
      if (requestLength < 5U) {
        return buildAttErrorResponse(opcode, 0U, kAttErrInvalidPdu, outAttResponse,
                                     outAttResponseLength);
      }

      const uint16_t rawStart = readLe16(&attRequest[1]);
      const uint16_t end = readLe16(&attRequest[3]);
      if (rawStart == 0U || rawStart > end) {
        return buildAttErrorResponse(opcode, rawStart, kAttErrInvalidHandle, outAttResponse,
                                     outAttResponseLength);
      }
      const uint16_t start = rawStart;

      outAttResponse[0] = kAttOpFindInfoRsp;
      outAttResponse[1] = 0U;
      uint16_t used = 2U;
      uint8_t responseFormat = 0U;
      uint16_t recordCount = 0U;
      for (size_t i = 0; i < (sizeof(kBleAttributeUuids) / sizeof(kBleAttributeUuids[0]));
           ++i) {
        const BleAttributeUuidRecord& record = kBleAttributeUuids[i];
        if (!inHandleRange(record.handle, start, end)) {
          continue;
        }
        if (responseFormat == kAttFindInfoFormat128Bit) {
          break;
        }
        if ((used + 4U) > maxAttResponseLen) {
          break;
        }
        responseFormat = kAttFindInfoFormat16Bit;
        outAttResponse[1] = responseFormat;
        writeLe16(&outAttResponse[used], record.handle);
        writeLe16(&outAttResponse[used + 2U], record.uuid16);
        used += 4U;
        ++recordCount;
      }
      for (uint8_t i = 0U;
           i < customGattServiceCount_; ++i) {
        const BleCustomServiceState& service = customGattServices_[i];
        if (inHandleRange(service.serviceHandle, start, end)) {
          if (recordCount == 0U) {
            outAttResponse[1] = 0x01U;
          }
          if (outAttResponse[1] != 0x01U || (used + 4U) > maxAttResponseLen) {
            break;
          }
          writeLe16(&outAttResponse[used], service.serviceHandle);
          writeLe16(&outAttResponse[used + 2U], kUuidPrimaryService);
          used += 4U;
          ++recordCount;
        }
        for (uint8_t j = 0U;
             j < customGattCharacteristicCount_; ++j) {
          const BleCustomCharacteristicState& ch = customGattCharacteristics_[j];
          if (ch.serviceHandle != service.serviceHandle) {
            continue;
          }
          if (inHandleRange(ch.declarationHandle, start, end)) {
            if (recordCount == 0U) {
              outAttResponse[1] = 0x01U;
            }
            if (outAttResponse[1] != 0x01U || (used + 4U) > maxAttResponseLen) {
              break;
            }
            writeLe16(&outAttResponse[used], ch.declarationHandle);
            writeLe16(&outAttResponse[used + 2U], kUuidCharacteristic);
            used += 4U;
            ++recordCount;
          }
          if (inHandleRange(ch.valueHandle, start, end)) {
            if (recordCount == 0U) {
              outAttResponse[1] = (ch.uuidSize == kBleUuid128Size) ? 0x02U : 0x01U;
            }
            if ((outAttResponse[1] == 0x01U && ch.uuidSize != kBleUuid16Size) ||
                (outAttResponse[1] == 0x02U && ch.uuidSize != kBleUuid128Size)) {
              break;
            }
            const uint16_t entrySize =
                (outAttResponse[1] == 0x02U) ? 18U : 4U;
            if ((used + entrySize) > maxAttResponseLen) {
              break;
            }
            writeLe16(&outAttResponse[used], ch.valueHandle);
            if (outAttResponse[1] == 0x02U) {
              writeUuidToAttWire(&outAttResponse[used + 2U], ch.uuid, kBleUuid128Size);
              used += 18U;
            } else {
              writeLe16(&outAttResponse[used + 2U], bleUuidRead16(ch.uuid, ch.uuidSize));
              used += 4U;
            }
            ++recordCount;
          }
          if (ch.cccdHandle != 0U &&
              inHandleRange(ch.cccdHandle, start, end)) {
            if (recordCount == 0U) {
              outAttResponse[1] = 0x01U;
            }
            if (outAttResponse[1] != 0x01U || (used + 4U) > maxAttResponseLen) {
              break;
            }
            writeLe16(&outAttResponse[used], ch.cccdHandle);
            writeLe16(&outAttResponse[used + 2U], kUuidClientCharacteristicConfig);
            used += 4U;
            ++recordCount;
          }
          if (ch.userDescriptionHandle != 0U &&
              inHandleRange(ch.userDescriptionHandle, start, end)) {
            if (recordCount == 0U) {
              outAttResponse[1] = 0x01U;
            }
            if (outAttResponse[1] != 0x01U || (used + 4U) > maxAttResponseLen) {
              break;
            }
            writeLe16(&outAttResponse[used], ch.userDescriptionHandle);
            writeLe16(&outAttResponse[used + 2U],
                      kUuidCharacteristicUserDescription);
            used += 4U;
            ++recordCount;
          }
          if (ch.presentationFormatHandle != 0U &&
              inHandleRange(ch.presentationFormatHandle, start, end)) {
            if (recordCount == 0U) {
              outAttResponse[1] = 0x01U;
            }
            if (outAttResponse[1] != 0x01U || (used + 4U) > maxAttResponseLen) {
              break;
            }
            writeLe16(&outAttResponse[used], ch.presentationFormatHandle);
            writeLe16(&outAttResponse[used + 2U],
                      kUuidCharacteristicPresentationFormat);
            used += 4U;
            ++recordCount;
          }
          if (ch.reportReferenceHandle != 0U &&
              inHandleRange(ch.reportReferenceHandle, start, end)) {
            if (recordCount == 0U) {
              outAttResponse[1] = 0x01U;
            }
            if (outAttResponse[1] != 0x01U || (used + 4U) > maxAttResponseLen) {
              break;
            }
            writeLe16(&outAttResponse[used], ch.reportReferenceHandle);
            writeLe16(&outAttResponse[used + 2U], kUuidReportReference);
            used += 4U;
            ++recordCount;
          }
        }
      }

      if (recordCount == 0U) {
        return buildAttErrorResponse(opcode, start, kAttErrAttributeNotFound,
                                     outAttResponse, outAttResponseLength);
      }
      *outAttResponseLength = used;
      return true;
    }

    case kAttOpFindByTypeValueReq: {
      emitBleTrace("ATT_RX_FIND_TYPE_REQ");
      if (requestLength < 9U) {
        return buildAttErrorResponse(opcode, 0U, kAttErrInvalidPdu, outAttResponse,
                                     outAttResponseLength);
      }

      const uint16_t rawStart = readLe16(&attRequest[1]);
      const uint16_t end = readLe16(&attRequest[3]);
      const uint16_t attrType = readLe16(&attRequest[5]);
      if (rawStart == 0U || rawStart > end) {
        return buildAttErrorResponse(opcode, rawStart, kAttErrInvalidHandle,
                                     outAttResponse, outAttResponseLength);
      }

      const uint16_t valueLen = static_cast<uint16_t>(requestLength - 7U);
      if (attrType != kUuidPrimaryService ||
          (valueLen != kBleUuid16Size && valueLen != kBleUuid128Size)) {
        return buildAttErrorResponse(opcode, rawStart, kAttErrAttributeNotFound,
                                     outAttResponse, outAttResponseLength);
      }

      const uint8_t* requestedServiceUuid = &attRequest[7];
      const uint16_t serviceUuid16 =
          (valueLen == kBleUuid16Size) ? readLe16(requestedServiceUuid) : 0U;
      if (valueLen == kBleUuid16Size) {
        emitAttServiceTrace(serviceUuid16);
      } else {
        emitBleTrace("ATT_RX_SVC_128");
      }
      outAttResponse[0] = kAttOpFindByTypeValueRsp;
      uint16_t used = 1U;
      const uint16_t maxRecords = static_cast<uint16_t>((maxAttResponseLen - 1U) / 4U);
      uint16_t recordCount = 0U;
      if (valueLen == kBleUuid16Size) {
        for (size_t i = 0; i < (sizeof(kBlePrimaryServices) / sizeof(kBlePrimaryServices[0]));
             ++i) {
          const BlePrimaryServiceRecord& service = kBlePrimaryServices[i];
          if (service.uuid16 != serviceUuid16) {
            continue;
          }
          if (!inHandleRange(service.startHandle, rawStart, end)) {
            continue;
          }
          if (recordCount >= maxRecords) {
            break;
          }
          writeLe16(&outAttResponse[used], service.startHandle);
          writeLe16(&outAttResponse[used + 2U], service.endHandle);
          used += 4U;
          ++recordCount;
        }
      }
      for (uint8_t i = 0U;
           (i < customGattServiceCount_) && (recordCount < maxRecords); ++i) {
        const BleCustomServiceState& service = customGattServices_[i];
        if (!bleUuidEqualsAttWire(service.uuid, service.uuidSize, requestedServiceUuid,
                                  static_cast<uint8_t>(valueLen))) {
          continue;
        }
        if (!inHandleRange(service.serviceHandle, rawStart, end)) {
          continue;
        }
        writeLe16(&outAttResponse[used], service.serviceHandle);
        writeLe16(&outAttResponse[used + 2U], service.endHandle);
        used += 4U;
        ++recordCount;
      }

      if (recordCount == 0U) {
        return buildAttErrorResponse(opcode, rawStart, kAttErrAttributeNotFound,
                                     outAttResponse, outAttResponseLength);
      }
      *outAttResponseLength = used;
      return true;
    }

    case kAttOpReadByGroupTypeReq: {
      emitBleTrace("ATT_RX_READ_GROUP_REQ");
      if (requestLength < 7U) {
        return buildAttErrorResponse(opcode, 0U, kAttErrInvalidPdu, outAttResponse,
                                     outAttResponseLength);
      }

      const uint16_t rawStart = readLe16(&attRequest[1]);
      const uint16_t end = readLe16(&attRequest[3]);
      if (rawStart == 0U || rawStart > end) {
        return buildAttErrorResponse(opcode, rawStart, kAttErrInvalidHandle,
                                     outAttResponse, outAttResponseLength);
      }

      const uint16_t groupTypeLen = static_cast<uint16_t>(requestLength - 5U);
      if (groupTypeLen != 2U) {
        if (groupTypeLen == 16U) {
          return buildAttErrorResponse(opcode, rawStart, kAttErrUnsupportedGroupType,
                                       outAttResponse, outAttResponseLength);
        }
        return buildAttErrorResponse(opcode, rawStart, kAttErrInvalidPdu,
                                     outAttResponse, outAttResponseLength);
      }

      const uint16_t start = rawStart;
      const uint16_t groupType = readLe16(&attRequest[5]);
      emitAttUuidTrace(groupType);
      if (groupType != kUuidPrimaryService) {
        return buildAttErrorResponse(opcode, start, kAttErrUnsupportedGroupType,
                                     outAttResponse, outAttResponseLength);
      }

      outAttResponse[0] = kAttOpReadByGroupTypeRsp;
      outAttResponse[1] = 0U;
      uint16_t used = 2U;
      uint8_t entryLen = 0U;
      uint16_t recordCount = 0U;
      for (size_t i = 0; i < (sizeof(kBlePrimaryServices) / sizeof(kBlePrimaryServices[0]));
           ++i) {
        const BlePrimaryServiceRecord& service = kBlePrimaryServices[i];
        if (!inHandleRange(service.startHandle, start, end)) {
          continue;
        }
        if (entryLen == 0U) {
          entryLen = 6U;
          outAttResponse[1] = entryLen;
        } else if (entryLen != 6U) {
          break;
        }
        if ((used + entryLen) > maxAttResponseLen) {
          break;
        }
        writeLe16(&outAttResponse[used], service.startHandle);
        writeLe16(&outAttResponse[used + 2U], service.endHandle);
        writeLe16(&outAttResponse[used + 4U], service.uuid16);
        used += 6U;
        ++recordCount;
      }
      for (uint8_t i = 0U;
           i < customGattServiceCount_; ++i) {
        const BleCustomServiceState& service = customGattServices_[i];
        if (!inHandleRange(service.serviceHandle, start, end)) {
          continue;
        }
        const uint8_t candidateEntryLen =
            (service.uuidSize == kBleUuid128Size) ? 20U : 6U;
        if (recordCount == 0U) {
          outAttResponse[1] = candidateEntryLen;
        }
        if (candidateEntryLen != outAttResponse[1] ||
            (used + candidateEntryLen) > maxAttResponseLen) {
          break;
        }
        writeLe16(&outAttResponse[used], service.serviceHandle);
        writeLe16(&outAttResponse[used + 2U], service.endHandle);
        if (candidateEntryLen == 20U) {
          writeUuidToAttWire(&outAttResponse[used + 4U], service.uuid, kBleUuid128Size);
        } else {
          writeLe16(&outAttResponse[used + 4U],
                    bleUuidRead16(service.uuid, service.uuidSize));
        }
        used += candidateEntryLen;
        ++recordCount;
      }

      if (recordCount == 0U) {
        return buildAttErrorResponse(opcode, start, kAttErrAttributeNotFound,
                                     outAttResponse, outAttResponseLength);
      }
      *outAttResponseLength = used;
      return true;
    }

    case kAttOpReadByTypeReq: {
      emitBleTrace("ATT_RX_READ_TYPE_REQ");
      if (requestLength < 7U) {
        return buildAttErrorResponse(opcode, 0U, kAttErrInvalidPdu, outAttResponse,
                                     outAttResponseLength);
      }

      const uint16_t rawStart = readLe16(&attRequest[1]);
      const uint16_t end = readLe16(&attRequest[3]);
      const uint16_t typeLen = static_cast<uint16_t>(requestLength - 5U);
      if (typeLen != kBleUuid16Size && typeLen != kBleUuid128Size) {
        return buildAttErrorResponse(opcode, 0U, kAttErrInvalidPdu, outAttResponse,
                                     outAttResponseLength);
      }
      const uint8_t* requestedType = &attRequest[5];
      const uint16_t type =
          (typeLen == kBleUuid16Size) ? readLe16(requestedType) : 0U;
      if (typeLen == kBleUuid16Size) {
        emitAttUuidTrace(type);
      } else {
        emitBleTrace("ATT_RX_UUID_128");
      }
      if (rawStart == 0U || rawStart > end) {
        return buildAttErrorResponse(opcode, rawStart, kAttErrInvalidHandle, outAttResponse,
                                     outAttResponseLength);
      }
      const uint16_t start = rawStart;
      const uint8_t typeLength = static_cast<uint8_t>(requestLength - 5U);
      if (typeLength != kBleUuidLength16 && typeLength != kBleUuidLength128) {
        return buildAttErrorResponse(opcode, rawStart, kAttErrInvalidPdu, outAttResponse,
                                     outAttResponseLength);
      }
      const uint16_t type16 =
          (typeLength == kBleUuidLength16) ? readLe16(&attRequest[5]) : 0U;

      outAttResponse[0] = kAttOpReadByTypeRsp;
      uint16_t used = 2U;
      uint8_t entryLen = 0U;
      uint16_t recordCount = 0U;
      uint16_t firstSecurityBlockedHandle = 0U;
      uint8_t firstSecurityBlockedError = 0U;

      if (typeLen == kBleUuid16Size && type == kUuidCharacteristic) {
        entryLen = 7U;
        outAttResponse[1] = entryLen;
        for (size_t i = 0; i < (sizeof(kBleCharacteristics) / sizeof(kBleCharacteristics[0]));
             ++i) {
          const BleCharacteristicRecord& ch = kBleCharacteristics[i];
          if (!inHandleRange(ch.declarationHandle, start, end)) {
            continue;
          }
          const uint8_t candidateEntryLen = 7U;
          if (entryLen == 0U) {
            entryLen = candidateEntryLen;
            outAttResponse[1] = entryLen;
          } else if (candidateEntryLen != entryLen) {
            break;
          }
          if ((used + entryLen) > maxAttResponseLen) {
            break;
          }
          writeLe16(&outAttResponse[used], ch.declarationHandle);
          outAttResponse[used + 2U] = ch.properties;
          writeLe16(&outAttResponse[used + 3U], ch.valueHandle);
          writeLe16(&outAttResponse[used + 5U], ch.uuid16);
          used += entryLen;
          ++recordCount;
        }
        for (uint8_t i = 0U;
             i < customGattCharacteristicCount_; ++i) {
          const BleCustomCharacteristicState& ch = customGattCharacteristics_[i];
          if (!inHandleRange(ch.declarationHandle, start, end)) {
            continue;
          }
          const uint8_t candidateEntryLen =
              static_cast<uint8_t>(2U + bleCharacteristicDeclarationValueLen(ch.uuidSize));
          if (candidateEntryLen == 2U) {
            break;
          }
          if (recordCount == 0U) {
            entryLen = candidateEntryLen;
            outAttResponse[1] = entryLen;
          }
          if (candidateEntryLen != entryLen ||
              (used + candidateEntryLen) > maxAttResponseLen) {
            break;
          }
          writeLe16(&outAttResponse[used], ch.declarationHandle);
          writeCharacteristicDeclarationValue(&outAttResponse[used + 2U], ch.properties,
                                              ch.valueHandle, ch.uuid, ch.uuidSize);
          used += candidateEntryLen;
          ++recordCount;
        }
      } else if (typeLen == kBleUuid16Size && type == kUuidPrimaryService) {
        entryLen = 4U;
        outAttResponse[1] = entryLen;
        for (size_t i = 0; i < (sizeof(kBlePrimaryServices) / sizeof(kBlePrimaryServices[0]));
             ++i) {
          const BlePrimaryServiceRecord& service = kBlePrimaryServices[i];
          if (!inHandleRange(service.startHandle, start, end)) {
            continue;
          }
          const uint8_t candidateEntryLen = 4U;
          if (entryLen == 0U) {
            entryLen = candidateEntryLen;
            outAttResponse[1] = entryLen;
          } else if (candidateEntryLen != entryLen) {
            break;
          }
          if ((used + entryLen) > maxAttResponseLen) {
            break;
          }
          writeLe16(&outAttResponse[used], service.startHandle);
          writeLe16(&outAttResponse[used + 2U], service.uuid16);
          used += entryLen;
          ++recordCount;
        }
        for (uint8_t i = 0U;
             i < customGattServiceCount_; ++i) {
          const BleCustomServiceState& service = customGattServices_[i];
          if (!inHandleRange(service.serviceHandle, start, end)) {
            continue;
          }
          const uint8_t candidateEntryLen =
              (service.uuidSize == kBleUuid128Size) ? 18U : 4U;
          if (recordCount == 0U) {
            entryLen = candidateEntryLen;
            outAttResponse[1] = entryLen;
          }
          if (candidateEntryLen != entryLen ||
              (used + candidateEntryLen) > maxAttResponseLen) {
            break;
          }
          writeLe16(&outAttResponse[used], service.serviceHandle);
          if (candidateEntryLen == 18U) {
            writeUuidToAttWire(&outAttResponse[used + 2U], service.uuid, kBleUuid128Size);
          } else {
            writeLe16(&outAttResponse[used + 2U],
                      bleUuidRead16(service.uuid, service.uuidSize));
          }
          used += candidateEntryLen;
          ++recordCount;
        }
      } else {
        constexpr uint8_t kReadByTypeScratchLen = 31U;
        const uint8_t readByTypeValueLimit =
            static_cast<uint8_t>(minU16(
                kReadByTypeScratchLen,
                (maxAttResponseLen > 4U)
                    ? static_cast<uint16_t>(maxAttResponseLen - 4U)
                    : 0U));
        uint16_t handles[8] = {0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U};
        uint8_t handleCount = 0U;
        if (typeLength == kBleUuidLength16 && type16 == kUuidDeviceName) {
          handles[handleCount++] = kHandleGapDeviceNameValue;
        } else if (typeLength == kBleUuidLength16 && type16 == kUuidAppearance) {
          handles[handleCount++] = kHandleGapAppearanceValue;
        } else if (typeLength == kBleUuidLength16 && type16 == kUuidPpcp) {
          handles[handleCount++] = kHandleGapPpcpValue;
        } else if (typeLength == kBleUuidLength16 && type16 == kUuidServiceChanged) {
          handles[handleCount++] = kHandleGattServiceChangedValue;
        } else if (typeLength == kBleUuidLength16 &&
                   type16 == kUuidClientSupportedFeatures) {
          // Some Android HID hosts probe 0x2B29 before service discovery and
          // stall if it is returned before pairing. Suppress only that direct
          // bootstrap lookup; normal discovery and encrypted access remain.
          if (gattClientFeaturesPreEncryptionVisible_ ||
              isConnectionEncrypted()) {
            handles[handleCount++] = kHandleGattClientFeaturesValue;
          }
        } else if (typeLength == kBleUuidLength16 &&
                   type16 == kUuidDatabaseHash) {
          handles[handleCount++] = kHandleGattDatabaseHashValue;
        } else if (typeLength == kBleUuidLength16 &&
                   type16 == kUuidClientCharacteristicConfig) {
          handles[handleCount++] = kHandleGattServiceChangedCccd;
          handles[handleCount++] = kHandleBatteryLevelCccd;
        } else if (typeLength == kBleUuidLength16 && type16 == kUuidBatteryLevel) {
          handles[handleCount++] = kHandleBatteryLevelValue;
        }

        for (uint8_t i = 0U; i < handleCount; ++i) {
          const uint16_t handle = handles[i];
          if (!inHandleRange(handle, start, end)) {
            continue;
          }
          uint8_t temp[kReadByTypeScratchLen] = {0};
          const uint8_t vlen =
              readAttributeValue(handle, 0U, temp, readByTypeValueLimit);
          const uint8_t readError =
              attReadSentinelToError(vlen, kAttErrReadNotPermitted);
          if (readError != 0U) {
            if (readError == kAttErrInsufficientEncryption ||
                readError == kAttErrInsufficientAuthentication) {
              if (recordCount == 0U) {
                return buildAttErrorResponse(opcode, handle, readError,
                                             outAttResponse, outAttResponseLength);
              }
              if (firstSecurityBlockedHandle == 0U) {
                firstSecurityBlockedHandle = handle;
                firstSecurityBlockedError = readError;
              }
              break;
            }
            continue;
          }

          const uint8_t candidateEntryLen = static_cast<uint8_t>(2U + vlen);
          if (candidateEntryLen == 2U) {
            continue;
          }
          if (entryLen == 0U) {
            entryLen = candidateEntryLen;
            outAttResponse[1] = entryLen;
          } else if (candidateEntryLen != entryLen) {
            break;
          }
          if ((used + entryLen) > maxAttResponseLen) {
            break;
          }

          writeLe16(&outAttResponse[used], handle);
          if (vlen > 0U) {
            memcpy(&outAttResponse[used + 2U], temp, vlen);
          }
          used += entryLen;
          ++recordCount;
        }

        for (uint8_t i = 0U; i < customGattCharacteristicCount_; ++i) {
          const BleCustomCharacteristicState& ch = customGattCharacteristics_[i];
          uint16_t handle = 0U;
          if (bleUuidEqualsAttWire(ch.uuid, ch.uuidSize, requestedType,
                                   static_cast<uint8_t>(typeLen))) {
            handle = ch.valueHandle;
          } else if (typeLen == kBleUuid16Size &&
                     type == kUuidClientCharacteristicConfig) {
            handle = ch.cccdHandle;
          } else if (typeLen == kBleUuid16Size &&
                     type == kUuidCharacteristicUserDescription) {
            handle = ch.userDescriptionHandle;
          } else if (typeLen == kBleUuid16Size &&
                     type == kUuidCharacteristicPresentationFormat) {
            handle = ch.presentationFormatHandle;
          } else if (typeLen == kBleUuid16Size &&
                     type == kUuidReportReference) {
            handle = ch.reportReferenceHandle;
          }
          if (handle == 0U || !inHandleRange(handle, start, end)) {
            continue;
          }

          uint8_t temp[kReadByTypeScratchLen] = {0};
          const uint8_t vlen =
              readAttributeValue(handle, 0U, temp, readByTypeValueLimit);
          const uint8_t readError =
              attReadSentinelToError(vlen, kAttErrReadNotPermitted);
          if (readError != 0U) {
            if (readError == kAttErrInsufficientEncryption ||
                readError == kAttErrInsufficientAuthentication) {
              if (recordCount == 0U) {
                return buildAttErrorResponse(opcode, handle, readError,
                                             outAttResponse, outAttResponseLength);
              }
              if (firstSecurityBlockedHandle == 0U) {
                firstSecurityBlockedHandle = handle;
                firstSecurityBlockedError = readError;
              }
              break;
            }
            continue;
          }

          const uint8_t candidateEntryLen = static_cast<uint8_t>(2U + vlen);
          if (candidateEntryLen == 2U) {
            continue;
          }
          if (entryLen == 0U) {
            entryLen = candidateEntryLen;
            outAttResponse[1] = entryLen;
          } else if (candidateEntryLen != entryLen) {
            break;
          }
          if ((used + entryLen) > maxAttResponseLen) {
            break;
          }

          writeLe16(&outAttResponse[used], handle);
          if (vlen > 0U) {
            memcpy(&outAttResponse[used + 2U], temp, vlen);
          }
          used += entryLen;
          ++recordCount;
        }
      }

      if (recordCount == 0U) {
        if (firstSecurityBlockedHandle != 0U && firstSecurityBlockedError != 0U) {
          return buildAttErrorResponse(opcode, firstSecurityBlockedHandle,
                                       firstSecurityBlockedError, outAttResponse,
                                       outAttResponseLength);
        }
        return buildAttErrorResponse(opcode, start, kAttErrAttributeNotFound,
                                     outAttResponse, outAttResponseLength);
      }
      *outAttResponseLength = used;
      if (typeLength == kBleUuidLength16 && type16 == kUuidDatabaseHash) {
        connectionGattDatabaseHashRead_ = true;
        emitBleTrace("GATT_DATABASE_HASH_READ");
      }
      return true;
    }

    case kAttOpReadReq: {
      emitBleTrace("ATT_RX_READ_REQ");
      if (requestLength < 3U) {
        return buildAttErrorResponse(opcode, 0U, kAttErrInvalidPdu, outAttResponse,
                                     outAttResponseLength);
      }
      const uint16_t handle = readLe16(&attRequest[1]);
      emitAttHandleTrace(handle);
      if (handle == 0U) {
        return buildAttErrorResponse(opcode, handle, kAttErrInvalidHandle,
                                     outAttResponse, outAttResponseLength);
      }
      const BleCustomCharacteristicState* authorizeTarget =
          findCustomCharacteristicByValueHandle(handle);
      outAttResponse[0] = kAttOpReadRsp;
      const uint8_t vlen =
          readAttributeValue(handle, 0U, &outAttResponse[1], maxAttValueLen);
      const uint8_t readError =
          attReadSentinelToError(vlen, kAttErrReadNotPermitted);
      if (readError != 0U) {
        return buildAttErrorResponse(opcode, handle, readError,
                                     outAttResponse, outAttResponseLength);
      }
      if (authorizeTarget != nullptr && authorizeTarget->readAuthorize) {
        if (!beginCustomGattAuthorization(
                BleGattAuthorizeType::kRead, opcode, 0U, authorizeTarget, 0U,
                nullptr, 0U)) {
          return buildAttErrorResponse(opcode, handle,
                                       kAttErrInsufficientAuthorization,
                                       outAttResponse,
                                       outAttResponseLength);
        }
        *outAttResponseLength = 0U;
        return false;
      }
      *outAttResponseLength = static_cast<uint16_t>(1U + vlen);
      if (handle == kHandleGattDatabaseHashValue) {
        connectionGattDatabaseHashRead_ = true;
      }
      return true;
    }

    case kAttOpReadBlobReq: {
      emitBleTrace("ATT_RX_READ_BLOB_REQ");
      if (requestLength < 5U) {
        return buildAttErrorResponse(opcode, 0U, kAttErrInvalidPdu, outAttResponse,
                                     outAttResponseLength);
      }
      const uint16_t handle = readLe16(&attRequest[1]);
      const uint16_t offset = readLe16(&attRequest[3]);
      emitAttHandleTrace(handle);
      if (handle == 0U) {
        return buildAttErrorResponse(opcode, handle, kAttErrInvalidHandle,
                                     outAttResponse, outAttResponseLength);
      }

      const BleCustomCharacteristicState* authorizeTarget =
          findCustomCharacteristicByValueHandle(handle);
      outAttResponse[0] = kAttOpReadBlobRsp;
      const uint8_t vlen =
          readAttributeValue(handle, offset, &outAttResponse[1], maxAttValueLen);
      const uint8_t readError =
          attReadSentinelToError(vlen, kAttErrReadNotPermitted);
      if (readError != 0U) {
        return buildAttErrorResponse(opcode, handle, readError,
                                     outAttResponse, outAttResponseLength);
      }
      if (authorizeTarget != nullptr && authorizeTarget->readAuthorize) {
        if (!beginCustomGattAuthorization(
                BleGattAuthorizeType::kRead, opcode, 0U, authorizeTarget,
                offset, nullptr, 0U)) {
          return buildAttErrorResponse(opcode, handle,
                                       kAttErrInsufficientAuthorization,
                                       outAttResponse,
                                       outAttResponseLength);
        }
        *outAttResponseLength = 0U;
        return false;
      }
      *outAttResponseLength = static_cast<uint16_t>(1U + vlen);
      return true;
    }

    case kAttOpReadMultipleReq: {
      emitBleTrace("ATT_RX_READ_MULTI_REQ");
      if (requestLength < 5U || ((requestLength & 0x01U) == 0U)) {
        return buildAttErrorResponse(opcode, 0U, kAttErrInvalidPdu, outAttResponse,
                                     outAttResponseLength);
      }

      outAttResponse[0] = kAttOpReadMultipleRsp;
      uint16_t used = 1U;
      const uint16_t handleCount = static_cast<uint16_t>((requestLength - 1U) / 2U);
      for (uint16_t i = 0U; i < handleCount; ++i) {
        const uint16_t handle = readLe16(&attRequest[1U + (i * 2U)]);
        if (handle == 0U) {
          return buildAttErrorResponse(opcode, handle, kAttErrInvalidHandle,
                                       outAttResponse, outAttResponseLength);
        }

        if (used >= maxAttResponseLen) {
          break;
        }

        const uint8_t chunkMax = static_cast<uint8_t>(maxAttResponseLen - used);
        const uint8_t vlen = readAttributeValue(handle, 0U, &outAttResponse[used], chunkMax);
        const uint8_t readError =
            attReadSentinelToError(vlen, kAttErrReadNotPermitted);
        if (readError != 0U) {
          return buildAttErrorResponse(opcode, handle, readError,
                                       outAttResponse, outAttResponseLength);
        }
        used = static_cast<uint16_t>(used + vlen);
      }

      if (used <= 1U) {
        return buildAttErrorResponse(opcode, 0U, kAttErrAttributeNotFound,
                                     outAttResponse, outAttResponseLength);
      }
      *outAttResponseLength = used;
      return true;
    }

    case kAttOpReadMultipleVariableReq: {
      emitBleTrace("ATT_RX_READ_MULTI_VAR_REQ");
      if (requestLength < 5U || ((requestLength & 0x01U) == 0U)) {
        return buildAttErrorResponse(opcode, 0U, kAttErrInvalidPdu, outAttResponse,
                                     outAttResponseLength);
      }

      outAttResponse[0] = kAttOpReadMultipleVariableRsp;
      uint16_t used = 1U;
      const uint16_t handleCount = static_cast<uint16_t>((requestLength - 1U) / 2U);
      for (uint16_t i = 0U; i < handleCount; ++i) {
        const uint16_t handle = readLe16(&attRequest[1U + (i * 2U)]);
        if (handle == 0U) {
          return buildAttErrorResponse(opcode, handle, kAttErrInvalidHandle,
                                       outAttResponse, outAttResponseLength);
        }

        uint8_t temp[kCustomGattMaxValueLength] = {0};
        const uint8_t tempMaxLen =
            static_cast<uint8_t>(minU16(maxAttResponseLen, 0xFFU));
        const uint8_t vlen =
            readAttributeValue(handle, 0U, temp, tempMaxLen);
        const uint8_t readError =
            attReadSentinelToError(vlen, kAttErrReadNotPermitted);
        if (readError != 0U) {
          return buildAttErrorResponse(opcode, handle, readError,
                                       outAttResponse, outAttResponseLength);
        }

        // Do not split the two-octet Value Length field. If the response is
        // full here, the next tuple is simply omitted from this PDU.
        if ((used + 2U) > maxAttResponseLen) {
          break;
        }

        writeLe16(&outAttResponse[used], vlen);
        used = static_cast<uint16_t>(used + 2U);

        const uint16_t available =
            static_cast<uint16_t>(maxAttResponseLen - used);
        const uint8_t toCopy =
            (vlen < available) ? vlen : static_cast<uint8_t>(available);
        if (toCopy > 0U) {
          memcpy(&outAttResponse[used], temp, toCopy);
          used = static_cast<uint16_t>(used + toCopy);
        }

        if (toCopy < vlen || used >= maxAttResponseLen) {
          break;
        }
      }

      if (used <= 1U) {
        return buildAttErrorResponse(opcode, 0U, kAttErrAttributeNotFound,
                                     outAttResponse, outAttResponseLength);
      }
      *outAttResponseLength = used;
      return true;
    }

    case kAttOpPrepareWriteReq: {
      emitBleTrace("ATT_RX_PREP_WRITE_REQ");
      if (requestLength < 5U) {
        return buildAttErrorResponse(opcode, 0U, kAttErrInvalidPdu, outAttResponse,
                                     outAttResponseLength);
      }
      if (requestLength > maxAttResponseLen) {
        return buildAttErrorResponse(opcode, 0U, kAttErrInvalidPdu, outAttResponse,
                                     outAttResponseLength);
      }

      const uint16_t handle = readLe16(&attRequest[1]);
      emitAttHandleTrace(handle);
      const bool isCccdHandle =
          (handle == kHandleGattServiceChangedCccd) ||
          (handle == kHandleBatteryLevelCccd) ||
          (findCustomCharacteristicByCccdHandle(handle) != nullptr);
      BleCustomCharacteristicState* customValueTarget =
          findCustomCharacteristicByValueHandle(handle);
      const bool isCustomValueHandle = (customValueTarget != nullptr);
      BleCustomCharacteristicState* customUserDescriptionTarget =
          findCustomCharacteristicByUserDescriptionHandle(handle);
      const bool isCustomUserDescriptionHandle =
          (customUserDescriptionTarget != nullptr);
      const bool isCustomVariableHandle =
          isCustomValueHandle || isCustomUserDescriptionHandle;

      if (!isCccdHandle && !isCustomVariableHandle) {
        return buildAttErrorResponse(opcode, handle, kAttErrWriteNotPermitted,
                                     outAttResponse, outAttResponseLength);
      }
      if (isCustomValueHandle &&
          ((customValueTarget->properties & kBleGattPropWrite) == 0U)) {
        return buildAttErrorResponse(opcode, handle, kAttErrWriteNotPermitted,
                                     outAttResponse, outAttResponseLength);
      }
      if (isCustomVariableHandle) {
        const BleCustomCharacteristicState* permissionTarget =
            isCustomValueHandle ? customValueTarget : customUserDescriptionTarget;
        uint8_t permissionError = kAttErrWriteNotPermitted;
        if (!customGattWritePermissionSatisfied(permissionTarget,
                                                &permissionError)) {
          return buildAttErrorResponse(opcode, handle, permissionError,
                                       outAttResponse, outAttResponseLength);
        }
      }

      const uint16_t offset = readLe16(&attRequest[3]);
      const uint16_t valueLen = static_cast<uint16_t>(requestLength - 5U);
      const uint16_t maxQueuedValueLen =
          isCustomValueHandle
              ? customValueTarget->maxValueLength
              : (isCustomUserDescriptionHandle ? kCustomGattMaxUserDescriptionLength
                                               : 2U);
      if (offset > maxQueuedValueLen) {
        return buildAttErrorResponse(opcode, handle, kAttErrInvalidOffset,
                                     outAttResponse, outAttResponseLength);
      }
      if ((offset + valueLen) > maxQueuedValueLen) {
        return buildAttErrorResponse(opcode, handle, kAttErrInvalidAttrValueLen,
                                     outAttResponse, outAttResponseLength);
      }

      if (!connectionPreparedWriteActive_) {
        clearPreparedWriteState();
        connectionPreparedWriteActive_ = true;
        connectionPreparedWriteHandle_ = handle;
        connectionPreparedWriteIsCustomVariable_ = isCustomVariableHandle;
      } else if (connectionPreparedWriteHandle_ != handle) {
        return buildAttErrorResponse(opcode, handle, kAttErrPrepareQueueFull,
                                     outAttResponse, outAttResponseLength);
      } else if (connectionPreparedWriteIsCustomVariable_ != isCustomVariableHandle) {
        return buildAttErrorResponse(opcode, handle, kAttErrPrepareQueueFull,
                                     outAttResponse, outAttResponseLength);
      }

      if (isCustomVariableHandle && offset > connectionPreparedWriteLength_) {
        return buildAttErrorResponse(opcode, handle, kAttErrInvalidOffset,
                                     outAttResponse, outAttResponseLength);
      }

      for (uint16_t i = 0U; i < valueLen; ++i) {
        const uint16_t pos = static_cast<uint16_t>(offset + i);
        connectionPreparedWriteValue_[pos] = attRequest[5U + i];
        if (!isCustomVariableHandle) {
          connectionPreparedWriteMask_ |= static_cast<uint8_t>(1U << pos);
        }
      }
      if (isCustomVariableHandle &&
          static_cast<uint16_t>(offset + valueLen) > connectionPreparedWriteLength_) {
        connectionPreparedWriteLength_ = static_cast<uint16_t>(offset + valueLen);
      }

      memcpy(outAttResponse, attRequest, requestLength);
      outAttResponse[0] = kAttOpPrepareWriteRsp;
      *outAttResponseLength = requestLength;
      return true;
    }

    case kAttOpExecuteWriteReq: {
      emitBleTrace("ATT_RX_EXEC_WRITE_REQ");
      if (requestLength != 2U) {
        return buildAttErrorResponse(opcode, 0U, kAttErrInvalidPdu, outAttResponse,
                                     outAttResponseLength);
      }

      const uint8_t flags = attRequest[1];
      if (flags == 0x00U) {
        clearPreparedWriteState();
        outAttResponse[0] = kAttOpExecuteWriteRsp;
        *outAttResponseLength = 1U;
        return true;
      }
      if (flags != 0x01U) {
        return buildAttErrorResponse(opcode, 0U, kAttErrInvalidPdu, outAttResponse,
                                     outAttResponseLength);
      }

      if (connectionPreparedWriteActive_) {
        const uint16_t handle = connectionPreparedWriteHandle_;
        if (connectionPreparedWriteIsCustomVariable_) {
          uint8_t errorCode = kAttErrWriteNotPermitted;
          if (!writeCustomGattCharacteristic(handle, connectionPreparedWriteValue_,
                                             connectionPreparedWriteLength_, true,
                                             &errorCode)) {
            clearPreparedWriteState();
            return buildAttErrorResponse(opcode, handle, errorCode, outAttResponse,
                                         outAttResponseLength);
          }
          clearPreparedWriteState();
        } else if ((handle != kHandleGattServiceChangedCccd) &&
                   (handle != kHandleBatteryLevelCccd) &&
                   (findCustomCharacteristicByCccdHandle(handle) == nullptr)) {
          clearPreparedWriteState();
          return buildAttErrorResponse(opcode, handle, kAttErrWriteNotPermitted,
                                       outAttResponse, outAttResponseLength);
        } else if ((connectionPreparedWriteMask_ & 0x03U) != 0x03U) {
          clearPreparedWriteState();
          return buildAttErrorResponse(opcode, handle, kAttErrInvalidAttrValueLen,
                                       outAttResponse, outAttResponseLength);
        } else if (!applyCccdState(handle, readLe16(&connectionPreparedWriteValue_[0]))) {
          clearPreparedWriteState();
          return buildAttErrorResponse(opcode, handle, kAttErrWriteNotPermitted,
                                       outAttResponse, outAttResponseLength);
        } else {
          persistBondedCccdState();
          armConnectionResyncWindow(kBleConnResyncExtraListenUs, 12U);
          clearPreparedWriteState();
        }
      }

      outAttResponse[0] = kAttOpExecuteWriteRsp;
      *outAttResponseLength = 1U;
      return true;
    }

    case kAttOpHandleValueCfm: {
      emitBleTrace("ATT_RX_HANDLE_CFM");
      // Confirmation for ATT Handle Value Indication. This opcode has no response.
      if (requestLength == 1U) {
        const bool confirmsServiceChanged =
            connectionServiceChangedIndicationAwaitingConfirm_;
        const uint32_t confirmedGeneration =
            connectionServiceChangedIndicationGeneration_;
        connectionServiceChangedIndicationAwaitingConfirm_ = false;
        connectionServiceChangedIndicationStartHandle_ = 0U;
        connectionServiceChangedIndicationEndHandle_ = 0U;
        connectionServiceChangedIndicationGeneration_ = 0U;
        if (confirmsServiceChanged) {
          (void)confirmBondedServiceChanged(confirmedGeneration);
        } else {
          connectionCustomIndicationAwaitingHandle_ = 0U;
        }
      }
      *outAttResponseLength = 0U;
      return false;
    }

    case kAttOpWriteReq: {
      emitBleTrace("ATT_RX_WRITE_REQ");
      if (requestLength < 3U) {
        return buildAttErrorResponse(opcode, 0U, kAttErrInvalidPdu, outAttResponse,
                                     outAttResponseLength);
      }
      const uint16_t handle = readLe16(&attRequest[1]);
      emitAttHandleTrace(handle);
      const uint16_t valueLen = static_cast<uint16_t>(requestLength - 3U);
      const uint8_t* value = (valueLen > 0U) ? &attRequest[3] : nullptr;

      if (handle == kHandleGattClientFeaturesValue) {
        uint8_t errorCode = kAttErrValueNotAllowed;
        if (!applyGattClientSupportedFeatures(value, valueLen, &errorCode)) {
          return buildAttErrorResponse(opcode, handle, errorCode,
                                       outAttResponse, outAttResponseLength);
        }
        outAttResponse[0] = kAttOpWriteRsp;
        *outAttResponseLength = 1U;
        return true;
      }

      const bool isCccdHandle =
          (handle == kHandleGattServiceChangedCccd) ||
          (handle == kHandleBatteryLevelCccd) ||
          (findCustomCharacteristicByCccdHandle(handle) != nullptr);
      if (isCccdHandle) {
        if (valueLen != 2U) {
          return buildAttErrorResponse(opcode, handle, kAttErrInvalidAttrValueLen,
                                       outAttResponse, outAttResponseLength);
        }
        const uint16_t cccd = readLe16(&attRequest[3]);
        if (!applyCccdState(handle, cccd)) {
          return buildAttErrorResponse(opcode, handle, kAttErrWriteNotPermitted,
                                       outAttResponse, outAttResponseLength);
        }
        persistBondedCccdState();
        armConnectionResyncWindow(kBleConnResyncExtraListenUs, 12U);
        if (connectionPreparedWriteActive_ &&
            connectionPreparedWriteHandle_ == handle) {
          clearPreparedWriteState();
        }
        outAttResponse[0] = kAttOpWriteRsp;
        *outAttResponseLength = 1U;
        return true;
      }
      BleCustomCharacteristicState* authorizeTarget =
          findCustomCharacteristicByValueHandle(handle);
      if (authorizeTarget != nullptr && authorizeTarget->writeAuthorize) {
        if ((authorizeTarget->properties & kBleGattPropWrite) == 0U) {
          return buildAttErrorResponse(opcode, handle,
                                       kAttErrWriteNotPermitted,
                                       outAttResponse,
                                       outAttResponseLength);
        }
        uint8_t permissionError = kAttErrWriteNotPermitted;
        if (!customGattWritePermissionSatisfied(authorizeTarget,
                                                &permissionError)) {
          return buildAttErrorResponse(opcode, handle, permissionError,
                                       outAttResponse,
                                       outAttResponseLength);
        }
        const bool lengthAllowed =
            authorizeTarget->fixedValueLength
                ? (valueLen == authorizeTarget->maxValueLength)
                : (valueLen <= authorizeTarget->maxValueLength);
        if (!lengthAllowed) {
          return buildAttErrorResponse(opcode, handle,
                                       kAttErrInvalidAttrValueLen,
                                       outAttResponse,
                                       outAttResponseLength);
        }
        if (!beginCustomGattAuthorization(
                BleGattAuthorizeType::kWrite, opcode, 0x01U,
                authorizeTarget, 0U, value, valueLen)) {
          return buildAttErrorResponse(opcode, handle,
                                       kAttErrInsufficientAuthorization,
                                       outAttResponse,
                                       outAttResponseLength);
        }
        *outAttResponseLength = 0U;
        return false;
      }
      uint8_t errorCode = kAttErrWriteNotPermitted;
      if (writeCustomGattCharacteristic(handle, value, valueLen, true, &errorCode)) {
        outAttResponse[0] = kAttOpWriteRsp;
        *outAttResponseLength = 1U;
        return true;
      }
      return buildAttErrorResponse(opcode, handle, errorCode, outAttResponse,
                                   outAttResponseLength);
    }

    case kAttOpWriteCmd: {
      emitBleTrace("ATT_RX_WRITE_CMD");
      if (requestLength >= 3U) {
        const uint16_t handle = readLe16(&attRequest[1]);
        emitAttHandleTrace(handle);
        const uint16_t valueLen = static_cast<uint16_t>(requestLength - 3U);
        const uint8_t* value = (valueLen > 0U) ? &attRequest[3] : nullptr;
        const bool isCccdHandle =
            (handle == kHandleGattServiceChangedCccd) ||
            (handle == kHandleBatteryLevelCccd) ||
            (findCustomCharacteristicByCccdHandle(handle) != nullptr);
        const bool isCustomValueHandle =
            (findCustomCharacteristicByValueHandle(handle) != nullptr);
        if (isCccdHandle && valueLen == 2U) {
          const uint16_t cccd = readLe16(&attRequest[3]);
          if (applyCccdState(handle, cccd)) {
            persistBondedCccdState();
            armConnectionResyncWindow(kBleConnResyncExtraListenUs, 12U);
            if (connectionPreparedWriteActive_ &&
                connectionPreparedWriteHandle_ == handle) {
              clearPreparedWriteState();
            }
          }
        } else if (isCustomValueHandle) {
          if (connectionPreparedWriteActive_ &&
              connectionPreparedWriteHandle_ == handle) {
            clearPreparedWriteState();
          }
          if (!enqueueDeferredGattWrite(&BleRadio::deferredCustomGattPeerWriteCallback,
                                        this, handle, value,
                                        static_cast<uint8_t>(valueLen), false)) {
            uint8_t ignoredError = kAttErrWriteNotPermitted;
            writeCustomGattCharacteristic(handle, value, valueLen, false,
                                         &ignoredError);
          }
        } else {
          uint8_t ignoredError = kAttErrWriteNotPermitted;
          writeCustomGattCharacteristic(handle, value, valueLen, false, &ignoredError);
        }
      }
      // Write command has no ATT response.
      *outAttResponseLength = 0U;
      return false;
    }

    case kAttOpSignedWriteCmd: {
      emitBleTrace("ATT_RX_SIGNED_WRITE_CMD");
      *outAttResponseLength = 0U;
      if (requestLength < 15U) {
        return false;
      }
      const uint16_t handle = readLe16(&attRequest[1]);
      emitAttHandleTrace(handle);
      uint32_t signCounter = 0U;
      if (!verifyAttSignedWrite(attRequest, requestLength, &signCounter)) {
        return false;
      }

      const uint16_t valueLength =
          static_cast<uint16_t>(requestLength - 15U);
      const uint8_t* value =
          (valueLength > 0U) ? &attRequest[3] : nullptr;
      if (!enqueueDeferredSignedGattWrite(handle, value, valueLength,
                                          signCounter)) {
        emitBleTrace("ATT_SIGNED_WRITE_DEFER_DROP");
      }
      return false;
    }

    case kAttOpErrorRsp:
    case kAttOpExchangeMtuRsp:
    case kAttOpFindInfoRsp:
    case kAttOpFindByTypeValueRsp:
    case kAttOpReadByTypeRsp:
    case kAttOpReadRsp:
    case kAttOpReadBlobRsp:
    case kAttOpReadMultipleRsp:
    case kAttOpReadMultipleVariableRsp:
    case kAttOpReadByGroupTypeRsp:
    case kAttOpWriteRsp:
    case kAttOpPrepareWriteRsp:
    case kAttOpExecuteWriteRsp:
    case kAttOpHandleValueNtf:
    case kAttOpHandleValueInd:
      *outAttResponseLength = 0U;
      return false;

    default:
      return buildAttErrorResponse(opcode, 0U, kAttErrRequestNotSupported,
                                   outAttResponse, outAttResponseLength);
  }
}

bool BleRadio::buildL2capResponse(uint8_t llid, const uint8_t* l2capPayload,
                                  uint8_t l2capPayloadLength,
                                  bool packetIsNew, uint8_t* outPayload,
                                  uint8_t* outPayloadLength) {
  const uint8_t* assembledPayload = nullptr;
  uint8_t assembledLength = 0U;
  if (!assembleIncomingL2capPayload(llid, l2capPayload, l2capPayloadLength,
                                    packetIsNew, &assembledPayload,
                                    &assembledLength)) {
    return false;
  }

  if (assembledPayload == nullptr || assembledLength < kBleL2capHeaderLen) {
    return false;
  }

  const uint16_t cid = readLe16(&assembledPayload[2]);
  const bool smpPairingHandshakeActive =
      (smpPairingState_ != kSmpPairingStateIdle) && !connectionEncSessionValid_;
  if (cid == kBleL2capCidAtt) {
    // During SMP confirm exchange, prioritize security-channel traffic and keep
    // ATT off the data path to reduce retransmission churn.
    if (smpPairingHandshakeActive) {
      return false;
    }
    return buildL2capAttResponse(assembledPayload, assembledLength, outPayload,
                                 outPayloadLength);
  }
  if (cid == kBleL2capCidLeSignaling) {
    return buildL2capSignalingResponse(assembledPayload, assembledLength,
                                       outPayload, outPayloadLength);
  }
  if (cid == kBleL2capCidSmp) {
    return buildL2capSmpResponse(assembledPayload, assembledLength,
                                 outPayload, outPayloadLength);
  }
  return false;
}

bool BleRadio::buildL2capSignalingResponse(const uint8_t* l2capPayload,
                                           uint8_t l2capPayloadLength,
                                           uint8_t* outPayload,
                                           uint8_t* outPayloadLength) {
  if (l2capPayload == nullptr || outPayload == nullptr || outPayloadLength == nullptr ||
      l2capPayloadLength < kBleL2capHeaderLen) {
    return false;
  }

  const uint16_t declaredL2capLength = readLe16(&l2capPayload[0]);
  const uint16_t cid = readLe16(&l2capPayload[2]);
  if (cid != kBleL2capCidLeSignaling) {
    return false;
  }

  const uint16_t available = static_cast<uint16_t>(l2capPayloadLength - kBleL2capHeaderLen);
  const uint16_t sigLength = minU16(declaredL2capLength, available);
  if (sigLength < 4U) {
    return false;
  }

  const uint8_t code = l2capPayload[kBleL2capHeaderLen + 0U];
  const uint8_t identifier = l2capPayload[kBleL2capHeaderLen + 1U];
  const uint16_t commandLength = readLe16(&l2capPayload[kBleL2capHeaderLen + 2U]);

  uint8_t outSigLen = 0U;
  auto writeCommandReject = [&](uint16_t reason) {
    outPayload[kBleL2capHeaderLen + 0U] = kL2capSigCodeCommandRejectRsp;
    outPayload[kBleL2capHeaderLen + 1U] = identifier;
    writeLe16(&outPayload[kBleL2capHeaderLen + 2U], 2U);
    writeLe16(&outPayload[kBleL2capHeaderLen + 4U], reason);
    outSigLen = 6U;
  };
  auto writeCommandRejectMtu = [&]() {
    outPayload[kBleL2capHeaderLen + 0U] = kL2capSigCodeCommandRejectRsp;
    outPayload[kBleL2capHeaderLen + 1U] = identifier;
    writeLe16(&outPayload[kBleL2capHeaderLen + 2U], 4U);
    writeLe16(&outPayload[kBleL2capHeaderLen + 4U],
              kL2capCmdRejectReasonSignalingMtuExceeded);
    writeLe16(&outPayload[kBleL2capHeaderLen + 6U], kBleL2capLeSignalingMtu);
    outSigLen = 8U;
  };
  auto writeCommandRejectInvalidCid = [&](uint16_t localCid, uint16_t remoteCid) {
    outPayload[kBleL2capHeaderLen + 0U] = kL2capSigCodeCommandRejectRsp;
    outPayload[kBleL2capHeaderLen + 1U] = identifier;
    writeLe16(&outPayload[kBleL2capHeaderLen + 2U], 6U);
    writeLe16(&outPayload[kBleL2capHeaderLen + 4U], kL2capCmdRejectReasonInvalidCid);
    writeLe16(&outPayload[kBleL2capHeaderLen + 6U], localCid);
    writeLe16(&outPayload[kBleL2capHeaderLen + 8U], remoteCid);
    outSigLen = 10U;
  };

  if ((4U + commandLength) > sigLength) {
    // Truncated signaling command payload; reject with signaling MTU hint.
    writeCommandRejectMtu();
  } else if (code == kL2capSigCodeCommandRejectRsp ||
             code == kL2capSigCodeConnParamUpdateRsp ||
             code == kL2capSigCodeLeCreditConnRsp) {
    // Response opcodes do not require another response.
    return false;
  } else if (code == kL2capSigCodeConnParamUpdateReq) {
    if (commandLength != 8U) {
      writeCommandReject(kL2capCmdRejectReasonCmdNotUnderstood);
    } else {
      const uint8_t* req = &l2capPayload[kBleL2capHeaderLen + 4U];
      const uint16_t intervalMin = readLe16(&req[0]);
      const uint16_t intervalMax = readLe16(&req[2]);
      const uint16_t latency = readLe16(&req[4]);
      const uint16_t timeout = readLe16(&req[6]);
      outPayload[kBleL2capHeaderLen + 0U] = kL2capSigCodeConnParamUpdateRsp;
      outPayload[kBleL2capHeaderLen + 1U] = identifier;
      writeLe16(&outPayload[kBleL2capHeaderLen + 2U], 2U);
      if ((connectionRole_ == BleConnectionRole::kCentral) &&
          !connectionCentralConnParamIndPending_ &&
          !connectionUpdatePending_ &&
          connParamsAreValid(intervalMin, intervalMax, latency, timeout)) {
        connectionCentralConnParamIntervalUnits_ =
            chooseAcceptedConnIntervalUnits(intervalMin, intervalMax);
        connectionCentralConnParamLatency_ = latency;
        connectionCentralConnParamTimeoutUnits_ = timeout;
        connectionCentralConnParamIndPending_ = true;
        writeLe16(&outPayload[kBleL2capHeaderLen + 4U],
                  kL2capConnParamResultAccepted);
      } else {
        writeLe16(&outPayload[kBleL2capHeaderLen + 4U],
                  kL2capConnParamResultRejected);
      }
      outSigLen = 6U;
    }
  } else if (code == kL2capSigCodeLeCreditConnReq) {
    if (commandLength != 10U) {
      writeCommandReject(kL2capCmdRejectReasonCmdNotUnderstood);
    } else {
      // LE Credit Based Connection Request:
      //   [PSM(2), SCID(2), MTU(2), MPS(2), InitialCredits(2)]
      // Peripheral path currently does not expose dynamic LE CoC channels.
      // Reply deterministically with "PSM not supported".
      outPayload[kBleL2capHeaderLen + 0U] = kL2capSigCodeLeCreditConnRsp;
      outPayload[kBleL2capHeaderLen + 1U] = identifier;
      writeLe16(&outPayload[kBleL2capHeaderLen + 2U], 10U);
      writeLe16(&outPayload[kBleL2capHeaderLen + 4U], 0x0000U);  // DCID
      writeLe16(&outPayload[kBleL2capHeaderLen + 6U], kBleL2capLeSignalingMtu);
      writeLe16(&outPayload[kBleL2capHeaderLen + 8U], kBleL2capLeSignalingMtu);
      writeLe16(&outPayload[kBleL2capHeaderLen + 10U], 0x0000U);  // Credits
      writeLe16(&outPayload[kBleL2capHeaderLen + 12U],
                kL2capLeCreditConnResultPsmNotSupported);
      outSigLen = 14U;
    }
  } else if (code == kL2capSigCodeLeFlowControlCredit) {
    if (commandLength != 4U) {
      writeCommandReject(kL2capCmdRejectReasonCmdNotUnderstood);
    } else {
      // No LE credit channels are established in this clean peripheral path.
      // Reject with Invalid CID and include received CID for diagnostics.
      const uint16_t cidFromPeer = readLe16(&l2capPayload[kBleL2capHeaderLen + 4U]);
      writeCommandRejectInvalidCid(0x0000U, cidFromPeer);
    }
  } else {
    writeCommandReject(kL2capCmdRejectReasonCmdNotUnderstood);
  }

  if ((kBleL2capHeaderLen + outSigLen) > kBleDataPduMaxPayload) {
    return false;
  }

  writeLe16(&outPayload[0], outSigLen);
  writeLe16(&outPayload[2], kBleL2capCidLeSignaling);
  *outPayloadLength = static_cast<uint8_t>(kBleL2capHeaderLen + outSigLen);
  return true;
}

bool BleRadio::buildL2capSmpResponse(const uint8_t* l2capPayload,
                                     uint8_t l2capPayloadLength,
                                     uint8_t* outPayload,
                                     uint8_t* outPayloadLength) {
  if (l2capPayload == nullptr || outPayload == nullptr || outPayloadLength == nullptr ||
      l2capPayloadLength < kBleL2capHeaderLen) {
    return false;
  }

  serviceSmpTimer();

  const uint16_t declaredL2capLength = readLe16(&l2capPayload[0]);
  const uint16_t cid = readLe16(&l2capPayload[2]);
  if (cid != kBleL2capCidSmp) {
    return false;
  }

  const uint16_t available = static_cast<uint16_t>(l2capPayloadLength - kBleL2capHeaderLen);
  const uint16_t smpLength = minU16(declaredL2capLength, available);
  if (smpLength < 1U) {
    return false;
  }

  const uint8_t* smp = &l2capPayload[kBleL2capHeaderLen];
  const uint8_t smpCode = smp[0];

  auto buildSmpResponse = [&](const uint8_t* smpData, uint8_t smpDataLen) -> bool {
    return buildSmpL2capResponse(smpData, smpDataLen, outPayload,
                                 outPayloadLength);
  };
  auto buildLocalSecureConnectionsPublicKeyResponse = [&]() -> bool {
    if (smpSecureConnectionsPublicKeySent_ ||
        !smpSecureConnectionsLocalKeyReady_) {
      return false;
    }

    uint8_t localPublicKey[kSmpPairingPublicKeyLen] = {0};
    buildSecureConnectionsWirePublicKeyFromInternalLe(
        smpSecureConnectionsPublicKey_,
        smpSecureConnectionsWireFormatBigEndian_, localPublicKey);
    localPublicKey[0] = kSmpCodePairingPublicKey;
    if (!buildSmpResponse(localPublicKey, sizeof(localPublicKey))) {
      return false;
    }

    smpSecureConnectionsPublicKeySent_ = true;
    smpSecureConnectionsDeferredPublicKey_ = false;
    emitBleTrace("SMP_SC_PUBKEY_TX");
    return true;
  };
  auto buildSmpPairingFailed = [&](uint8_t reason) -> bool {
    latchSecureConnectionsDebugState();
    smpPairingFailureReasonPending_ = true;
    smpLastPairingFailureReason_ = reason;
    uint8_t failed[2] = {kSmpCodePairingFailed, reason};
    const bool built = buildSmpResponse(failed, sizeof(failed));
    invalidateSecureConnectionsKeypairAndOob(
        smpSecureConnectionsLocalKeyReady_,
        smpSecureConnectionsLocalKeyInProgress_,
        smpSecureConnectionsPrivateKey_, smpSecureConnectionsPublicKey_,
        smpOobDataLocalValid_, smpOobDataRemoteValid_, smpOobLocal_,
        smpOobRemote_);
    clearSmpSecurityState();
    emitBleTrace("SMP_PAIRING_FAILED");
    return built;
  };
  if (smpTimedOutForConnection_) {
    return false;
  }
  if (smpCode == kSmpCodePairingRequest ||
      smpCode == kSmpCodeSecurityRequest) {
    if (smpBackoffActiveForCurrentPeer()) {
      smpPairingFailureReasonPending_ = true;
      smpLastPairingFailureReason_ = kSmpReasonRepeatedAttempts;
      const uint8_t repeatedAttempts[2] = {
          kSmpCodePairingFailed, kSmpReasonRepeatedAttempts};
      emitBleTrace("SMP_REPEATED_ATTEMPTS");
      return buildSmpResponse(repeatedAttempts, sizeof(repeatedAttempts));
    }
    restartSmpTimer();
  }
  if (smpCode == kSmpCodePairingFailed) {
    latchSecureConnectionsDebugState();
    smpPairingFailureReasonPending_ = true;
    smpLastPairingFailureReason_ =
        (smpLength >= 2U) ? smp[1] : kSmpReasonUnspecified;
    noteSmpPairingFailure(smpLastPairingFailureReason_);
    stopSmpTimer();
    invalidateSecureConnectionsKeypairAndOob(
        smpSecureConnectionsLocalKeyReady_,
        smpSecureConnectionsLocalKeyInProgress_,
        smpSecureConnectionsPrivateKey_, smpSecureConnectionsPublicKey_,
        smpOobDataLocalValid_, smpOobDataRemoteValid_, smpOobLocal_,
        smpOobRemote_);
    clearSmpSecurityState();
    emitBleTrace("SMP_PAIRING_FAILED_RX");
    return false;
  }

  switch (smpCode) {
    case kSmpCodePairingRequest: {
      if (smpLength != kSmpPairingRequestLen) {
        return buildSmpPairingFailed(kSmpReasonInvalidParameters);
      }
      if (smp[1] > kSmpIoCapKeyboardDisplay ||
          (smp[3] & kSmpAuthReqBondingMask) > 0x01U ||
          (smp[3] & 0xC0U) != 0U ||
          (smp[5] & static_cast<uint8_t>(~kSmpKeyDistDefinedMask)) != 0U ||
          (smp[6] & static_cast<uint8_t>(~kSmpKeyDistDefinedMask)) != 0U) {
        return buildSmpPairingFailed(kSmpReasonInvalidParameters);
      }
      if (!smpLocalInitiator_ && smpPairingState_ != kSmpPairingStateIdle) {
        if (memcmp(smpPairingReq_, smp, kSmpPairingRequestLen) == 0 &&
            smpPairingState_ >= kSmpPairingStateRspSent) {
          emitBleTrace("SMP_PAIRING_REQUEST_RETRY");
          return buildSmpResponse(smpPairingRsp_, kSmpPairingResponseLen);
        }
        return buildSmpPairingFailed(kSmpReasonUnspecified);
      }

      const uint8_t oobFlag = smp[2];
      const uint8_t maxKeySize = smp[4];
      const bool peerRequestedSecureConnections =
          (smp[3] & kSmpAuthReqSecureConnectionsMask) != 0U;
      const BleSecurityPolicy requestedPolicy = securityPolicy_;
      if (requestedPolicy.secureConnectionsRequired &&
          !peerRequestedSecureConnections) {
        return buildSmpPairingFailed(kSmpReasonAuthRequirements);
      }
      if (smpOobEnabled_ && !requestedPolicy.secureConnectionsEnabled) {
        return buildSmpPairingFailed(kSmpReasonOobNotAvailable);
      }
      if (oobFlag > 0x01U) {
        return buildSmpPairingFailed(kSmpReasonInvalidParameters);
      }
      const bool peerHasLocalOobData = oobFlag != 0x00U;
      const bool localHasPeerOobData =
          smpOobEnabled_ && smpOobDataRemoteValid_ &&
          peerRequestedSecureConnections;
      if (smpOobEnabled_ && !smpOobDataLocalValid_ &&
          !smpOobDataRemoteValid_) {
        return buildSmpPairingFailed(kSmpReasonOobNotAvailable);
      }
      if (!peerRequestedSecureConnections && smpOobEnabled_ &&
          (smpOobDataLocalValid_ || smpOobDataRemoteValid_)) {
        // This API carries LE SC r/c values, not a legacy 128-bit TK.
        return buildSmpPairingFailed(kSmpReasonOobNotAvailable);
      }
      if (peerHasLocalOobData &&
          (!peerRequestedSecureConnections || !smpOobEnabled_ ||
           !smpOobDataLocalValid_)) {
        return buildSmpPairingFailed(kSmpReasonOobNotAvailable);
      }
      if (maxKeySize < 7U || maxKeySize > 16U) {
        return buildSmpPairingFailed(kSmpReasonEncryptionKeySize);
      }
      if (connectionEncSessionValid_ || connectionEncRxEnabled_ ||
          connectionEncTxEnabled_) {
        return buildSmpPairingFailed(kSmpReasonPairingNotSupported);
      }

      const bool replacingPrimedBond = bondKeyPrimedForConnection_;
      clearSmpSecurityState();
      latchSecurityPolicyForPairing();
      if (replacingPrimedBond && bondRecordValid_) {
        // Keep the old bond until replacement pairing succeeds. Some Android
        // stacks retry pairing after a clean disconnect; erasing the previous
        // keys here leaves both sides with mismatched bond state if the retry
        // is interrupted.
        connectionServiceChangedIndicationsEnabled_ = false;
        connectionServiceChangedCccdRestored_ = false;
        connectionServiceChangedIndicationPending_ = false;
        connectionServiceChangedIndicationAwaitingConfirm_ = false;
        connectionServiceChangedIndicationStartHandle_ = 0U;
        connectionServiceChangedIndicationEndHandle_ = 0U;
        connectionServiceChangedIndicationGeneration_ = 0U;
        connectionBatteryNotificationsEnabled_ = false;
        connectionBatteryNotificationPending_ = false;
        clearCustomGattConnectionState();
        if (connectionPendingTxValid_ &&
            connectionPendingTxLlid_ == kBlePduDataStartOrComplete &&
            connectionPendingTxLength_ >= (kBleL2capHeaderLen + 1U) &&
            readLe16(&connectionPendingTxPayload_[2]) == kBleL2capCidAtt &&
            ((connectionPendingTxPayload_[4] == kAttOpHandleValueNtf) ||
             (connectionPendingTxPayload_[4] == kAttOpHandleValueInd))) {
          connectionPendingTxValid_ = false;
          connectionPendingTxLlid_ = 0x01U;
          connectionPendingTxLength_ = 0U;
          memset(connectionPendingTxPayload_, 0, sizeof(connectionPendingTxPayload_));
          connectionCustomIndicationAwaitingHandle_ = 0U;
          emitBleTrace("ATT_NOTIFY_DROP_PAIRING");
        }
        emitBleTrace("BOND_REPLACE_PENDING");
      }
      memcpy(smpPairingReq_, smp, kSmpPairingRequestLen);
      smpLocalInitiator_ = false;
      smpPairingFailureReasonPending_ = false;
      smpLastPairingFailureReason_ = 0U;
      smpPairingRsp_[0] = kSmpCodePairingResponse;
      smpPairingRsp_[1] = smpPairingLocalIoCapabilities_;
      // The response flag reports remote data already held by this device.
      // The request flag independently reports whether the peer holds ours.
      smpPairingRsp_[2] = localHasPeerOobData ? 0x01U : 0x00U;
      const uint8_t peerBonding = smp[3] & kSmpAuthReqBondingMask;
      const bool peerRequestedMitm =
          (smp[3] & kSmpAuthReqMitmMask) != 0U;
      smpPairingRsp_[3] = buildLocalPairingAuthReq(peerBonding != 0U);
      smpBondingRequested_ =
          (smpPairingRsp_[3] & kSmpAuthReqBondingMask) != 0U;
      smpPairingRsp_[4] =
          maxKeySize < smpSecurityPolicy_.maxEncryptionKeySize
              ? maxKeySize
              : smpSecurityPolicy_.maxEncryptionKeySize;
      if (smpPairingRsp_[4] < smpSecurityPolicy_.minEncryptionKeySize) {
        return buildSmpPairingFailed(kSmpReasonEncryptionKeySize);
      }
      uint8_t initiatorKeyDist = 0x00U;
      uint8_t responderKeyDist = 0x00U;
      if (smpBondingRequested_) {
        initiatorKeyDist = static_cast<uint8_t>(
            smp[5] & static_cast<uint8_t>(kSmpKeyDistEncKeyMask |
                                          kSmpKeyDistIdKeyMask |
                                          kSmpKeyDistSignKeyMask));
        responderKeyDist = static_cast<uint8_t>(
            smp[6] & static_cast<uint8_t>(kSmpKeyDistEncKeyMask |
                                          kSmpKeyDistIdKeyMask |
                                          kSmpKeyDistSignKeyMask));
      }
      if (peerRequestedSecureConnections &&
          smpSecurityPolicy_.secureConnectionsEnabled) {
        smpSecureConnectionsActive_ = true;
        smpPairingRsp_[3] |= kSmpAuthReqSecureConnectionsMask;
        // LE Secure Connections derives the link key from DHKey. Identity and
        // signing keys remain bidirectional so either role can resolve bonded
        // RPAs and use authenticated signed writes after reconnecting.
        initiatorKeyDist = static_cast<uint8_t>(
            smp[5] & static_cast<uint8_t>(
                kSmpKeyDistIdKeyMask | kSmpKeyDistSignKeyMask));
        responderKeyDist = static_cast<uint8_t>(
            smp[6] & static_cast<uint8_t>(
                kSmpKeyDistIdKeyMask | kSmpKeyDistSignKeyMask));
      } else {
        smpPairingRsp_[3] &=
            static_cast<uint8_t>(~kSmpAuthReqSecureConnectionsMask);
        if (smpBondingRequested_ &&
            (responderKeyDist & kSmpKeyDistEncKeyMask) != 0U &&
            !prepareLocalLegacyEncryptionKey()) {
          return buildSmpPairingFailed(kSmpReasonUnspecified);
        }
      }
      if (!localIdentityDistributionAvailable()) {
        responderKeyDist = static_cast<uint8_t>(
            responderKeyDist & ~kSmpKeyDistIdKeyMask);
      }
      smpPairingRsp_[5] = initiatorKeyDist;
      smpPairingRsp_[6] = responderKeyDist;
      smpExpectPeerEncKey_ = smpSecureConnectionsActive_
                                  ? false
                                  : ((smpPairingRsp_[5] &
                                      kSmpKeyDistEncKeyMask) != 0U);
      smpExpectPeerIdKey_ = ((smpPairingRsp_[5] & kSmpKeyDistIdKeyMask) != 0U);
      smpExpectPeerSignKey_ =
          ((smpPairingRsp_[5] & kSmpKeyDistSignKeyMask) != 0U);
      smpDistributeLocalEncKey_ =
          ((smpPairingRsp_[6] & kSmpKeyDistEncKeyMask) != 0U);
      smpDistributeLocalIdKey_ =
          ((smpPairingRsp_[6] & kSmpKeyDistIdKeyMask) != 0U);
      smpDistributeLocalSignKey_ =
          ((smpPairingRsp_[6] & kSmpKeyDistSignKeyMask) != 0U);
      smpLocalEncInfoSent_ = false;
      smpLocalEncInfoAcked_ = false;
      smpLocalMasterIdSent_ = false;
      smpLocalMasterIdAcked_ = false;
      smpLocalIdInfoSent_ = false;
      smpLocalIdInfoAcked_ = false;
      smpLocalIdAddressSent_ = false;
      smpLocalIdAddressAcked_ = false;
      smpLocalSignInfoSent_ = false;
      smpLocalSignInfoAcked_ = false;
      smpKeySize_ = smpPairingRsp_[4];
      smpPairingState_ = kSmpPairingStateRspSent;
      if (smpSecureConnectionsActive_) {
        const uint8_t pairAlg = resolveCurrentSecureConnectionsPairAlgorithm();
        if ((smpPairingRsp_[3] & kSmpAuthReqMitmMask) != 0U &&
            pairAlg == kSmpScPairAlgJustWorks) {
          emitBleTrace("SMP_SC_AUTH_REQUIREMENTS");
          return buildSmpPairingFailed(kSmpReasonAuthRequirements);
        }
        if (smpOobEnabled_ &&
            (smpOobDataLocalValid_ || smpOobDataRemoteValid_) &&
            pairAlg != kSmpScPairAlgOob) {
          emitBleTrace("SMP_SC_OOB_INCONSISTENT");
          return buildSmpPairingFailed(kSmpReasonOobNotAvailable);
        }
        if (!secureConnectionsPairAlgorithmSupported(pairAlg)) {
          emitBleTrace("SMP_SC_ALG_UNSUPPORTED");
          return buildSmpPairingFailed(
              (pairAlg == kSmpScPairAlgOob) ? kSmpReasonOobNotAvailable
                                            : kSmpReasonAuthRequirements);
        }
        emitBleTrace("SMP_SC_REQUEST_RX");
      } else {
        const uint8_t legacyAction = resolveLegacyPairingIoAction();
        if (smpSecurityPolicy_.mitmRequired &&
            legacyAction == kSmpScIoActionNone) {
          emitBleTrace("SMP_LEGACY_AUTH_REQUIREMENTS");
          return buildSmpPairingFailed(kSmpReasonAuthRequirements);
        }
        const BleSmpPasskeyPreparationResult passkeyResult =
            preparePairingPasskeyForAction(legacyAction);
        if (passkeyResult == BleSmpPasskeyPreparationResult::Fatal) {
          return buildSmpPairingFailed(kSmpReasonAuthRequirements);
        }
        smpLegacyConfirmDeferred_ =
            passkeyResult == BleSmpPasskeyPreparationResult::Pending;
      }
      if (peerRequestedMitm &&
          ((smpPairingRsp_[3] & kSmpAuthReqMitmMask) == 0U)) {
        // A phone or host may request MITM even when this peripheral has no
        // display/keyboard/passkey path. Keep the link alive by responding with
        // unauthenticated LE Secure Connections; peers that truly require MITM
        // can still reject the pairing on their side.
        emitBleTrace("SMP_MITM_REQUEST_JUST_WORKS_FALLBACK");
      }

      // Precompute local confirm material here so confirm/random stages avoid
      // repeated AES work while servicing tight connection events.
      if (!prepareLocalPairingRandom()) {
        return buildSmpPairingFailed(kSmpReasonUnspecified);
      }
      if (!peerRequestedSecureConnections) {
        if (!smpLegacyConfirmDeferred_) {
          uint8_t tk[16] = {0};
          buildLegacyTemporaryKey(tk);
          const uint8_t iat =
              connectionPeerAddressRandom_ ? 0x01U : 0x00U;
          const uint8_t rat =
              bleAddressTypeIsRandom(addressType_) ? 0x01U : 0x00U;
          if (!smpC1(tk, smpLocalRandom_, smpPairingReq_, smpPairingRsp_, iat,
                     connectionPeerAddress_, rat, address_,
                     smpLocalConfirm_)) {
            return buildSmpPairingFailed(kSmpReasonUnspecified);
          }
        }
      }

      emitBleTrace("SMP_PAIRING_REQUEST_RX");
      emitBleTrace("SMP_PAIRING_RESPONSE_TX");
      return buildSmpResponse(smpPairingRsp_, kSmpPairingResponseLen);
    }

    case kSmpCodePairingPublicKey: {
      if (smpLength != kSmpPairingPublicKeyLen) {
        return buildSmpPairingFailed(kSmpReasonInvalidParameters);
      }
      if (!smpSecureConnectionsActive_ ||
          smpPairingState_ != kSmpPairingStateRspSent) {
        return buildSmpPairingFailed(kSmpReasonUnspecified);
      }

      uint8_t peerPublicKeyWire[65] = {0};
      peerPublicKeyWire[0] = 0x04U;
      memcpy(&peerPublicKeyWire[1], &smp[1], 64U);
      uint8_t peerPublicKeyInternalLe[65] = {0};
      bool wireBigEndian = false;
      Secp256r1Point peerPoint{};
      if (!decodeSecureConnectionsPublicKeyToInternalLe(
              peerPublicKeyWire, peerPublicKeyInternalLe, &wireBigEndian,
              &peerPoint)) {
        smpSecureConnectionsLastPublicKeyDecodeFailed_ = true;
        smpSecureConnectionsLastPublicKeyDecodeLength_ =
            sizeof(peerPublicKeyWire);
        memcpy(smpSecureConnectionsLastPublicKeyWire_, peerPublicKeyWire,
               sizeof(peerPublicKeyWire));
        emitBleTrace("SMP_SC_PUBKEY_INVALID");
        return buildSmpPairingFailed(kSmpReasonDhKeyCheckFailed);
      }

      smpSecureConnectionsLastPublicKeyDecodeFailed_ = false;
      smpSecureConnectionsLastPublicKeyDecodeLength_ =
          sizeof(peerPublicKeyWire);
      memcpy(smpSecureConnectionsLastPublicKeyWire_, peerPublicKeyWire,
             sizeof(peerPublicKeyWire));
      memcpy(smpSecureConnectionsPeerPublicKey_, peerPublicKeyInternalLe,
             sizeof(smpSecureConnectionsPeerPublicKey_));
      smpSecureConnectionsWireFormatBigEndian_ = wireBigEndian;
      smpSecureConnectionsPeerPublicKeyValid_ = true;
      smpSecureConnectionsDhKeyReady_ = false;
      smpSecureConnectionsLocalConfirmReady_ = false;
      smpSecureConnectionsCheckValuesReady_ = false;
      smpSecureConnectionsReceivedDhKeyCheckValid_ = false;
      smpSecureConnectionsDeferredDhKeyCheck_ = false;
      memset(smpSecureConnectionsReceivedDhKeyCheck_, 0,
             sizeof(smpSecureConnectionsReceivedDhKeyCheck_));

      const uint8_t pairAlg = resolveCurrentSecureConnectionsPairAlgorithm();
      if (pairAlg == kSmpScPairAlgOob &&
          localHasPeerSecureConnectionsOobData(
              smpLocalInitiator_, smpPairingReq_, smpPairingRsp_)) {
        if (!smpOobDataRemoteValid_ ||
            !validateSecureConnectionsOobCommitment(
                smpSecureConnectionsPeerPublicKey_, smpOobRemote_)) {
          emitBleTrace("SMP_SC_OOB_CONFIRM_FAILED");
          return buildSmpPairingFailed(kSmpReasonConfirmValueFailed);
        }
        emitBleTrace("SMP_SC_OOB_CONFIRM_OK");
      }

      if (!smpSecureConnectionsLocalKeyReady_) {
        smpSecureConnectionsDeferredPublicKey_ = true;
        return false;
      }

      if (memcmp(&smpSecureConnectionsPeerPublicKey_[1],
                 &smpSecureConnectionsPublicKey_[1], 32U) == 0) {
        emitBleTrace("SMP_SC_PUBKEY_REJECTED");
        return buildSmpPairingFailed(kSmpReasonUnspecified);
      }
      const BleSmpPasskeyPreparationResult passkeyResult =
          prepareSecureConnectionsPasskey();
      if (passkeyResult == BleSmpPasskeyPreparationResult::Fatal) {
        return buildSmpPairingFailed(kSmpReasonAuthRequirements);
      }

      emitBleTrace("SMP_SC_PUBKEY_RX");
      if (smpLocalInitiator_) {
        if (pairAlg == kSmpScPairAlgOob) {
          smpSecureConnectionsDeferredConfirm_ = false;
          smpSecureConnectionsDeferredRandom_ = true;
        } else {
          smpSecureConnectionsDeferredConfirm_ =
              secureConnectionsInitiatorTransmitsConfirm();
        }
        if (buildLocalSecureConnectionsPublicKeyResponse()) {
          return true;
        }
        if (!smpSecureConnectionsPublicKeySent_) {
          smpSecureConnectionsDeferredPublicKey_ = true;
        }
        return false;
      }

      smpSecureConnectionsDeferredConfirm_ =
          (pairAlg == kSmpScPairAlgOob)
              ? false
              : !secureConnectionsInitiatorTransmitsConfirm();
      if (buildLocalSecureConnectionsPublicKeyResponse()) {
        return true;
      }
      smpSecureConnectionsDeferredPublicKey_ = true;
      return false;
    }

    case kSmpCodePairingConfirm: {
      if (smpLength != kSmpPairingConfirmLen) {
        return buildSmpPairingFailed(kSmpReasonInvalidParameters);
      }
      if (smpSecureConnectionsActive_ &&
          resolveCurrentSecureConnectionsPairAlgorithm() ==
              kSmpScPairAlgOob) {
        // LE SC OOB commitments are exchanged through the OOB channel. The
        // in-band phase starts with Pairing Random (Na/Nb), not Pairing Confirm.
        return buildSmpPairingFailed(kSmpReasonUnspecified);
      }
      const bool expectingResponderConfirm =
          smpLocalInitiator_ &&
          (smpPairingState_ == kSmpPairingStateConfirmSent);
      if (smpPairingState_ != kSmpPairingStateRspSent &&
          !expectingResponderConfirm) {
        return buildSmpPairingFailed(kSmpReasonUnspecified);
      }

      memcpy(smpPeerConfirm_, &smp[1], sizeof(smpPeerConfirm_));
      smpPeerConfirmReceived_ = true;
      if (smpSecureConnectionsActive_) {
        if (!smpSecureConnectionsPeerPublicKeyValid_ ||
            !smpSecureConnectionsPublicKeySent_) {
          return buildSmpPairingFailed(kSmpReasonUnspecified);
        }
        const BleSmpPasskeyPreparationResult passkeyResult =
            prepareSecureConnectionsPasskey();
        if (passkeyResult == BleSmpPasskeyPreparationResult::Pending) {
          smpSecureConnectionsDeferredConfirm_ = true;
          emitBleTrace("SMP_SC_CONFIRM_WAIT_PASSKEY");
          return false;
        }
        if (passkeyResult == BleSmpPasskeyPreparationResult::Fatal ||
            !computeSecureConnectionsLocalConfirm()) {
          return buildSmpPairingFailed(kSmpReasonUnspecified);
        }

        emitBleTrace("SMP_SC_CONFIRM_RX");
        if (smpLocalInitiator_) {
          smpSecureConnectionsDeferredRandom_ = true;
          return false;
        }
        if (smpSecureConnectionsConfirmSent_) {
          smpPairingState_ = kSmpPairingStateConfirmSent;
          return false;
        }

        smpSecureConnectionsDeferredConfirm_ = true;
        return false;
      }

      if (resolveLegacyPairingIoAction() == kSmpScIoActionInput &&
          !smpPairingPasskeyValid_) {
        smpLegacyConfirmDeferred_ = true;
        emitBleTrace("SMP_CONFIRM_WAIT_PASSKEY");
        return false;
      }
      if (smpLegacyConfirmDeferred_) {
        uint8_t tk[16] = {0};
        buildLegacyTemporaryKey(tk);
        const uint8_t initiatorType =
            smpLocalInitiator_
                ? (bleAddressTypeIsRandom(addressType_) ? 0x01U : 0x00U)
                : (connectionPeerAddressRandom_ ? 0x01U : 0x00U);
        const uint8_t responderType =
            smpLocalInitiator_
                ? (connectionPeerAddressRandom_ ? 0x01U : 0x00U)
                : (bleAddressTypeIsRandom(addressType_) ? 0x01U : 0x00U);
        const uint8_t* initiatorAddress =
            smpLocalInitiator_ ? address_ : connectionPeerAddress_;
        const uint8_t* responderAddress =
            smpLocalInitiator_ ? connectionPeerAddress_ : address_;
        if (!smpC1(tk, smpLocalRandom_, smpPairingReq_, smpPairingRsp_,
                   initiatorType, initiatorAddress, responderType,
                   responderAddress, smpLocalConfirm_)) {
          return buildSmpPairingFailed(kSmpReasonUnspecified);
        }
        smpLegacyConfirmDeferred_ = false;
      }

      if (smpLocalInitiator_) {
        uint8_t localRandom[17] = {0};
        localRandom[0] = kSmpCodePairingRandom;
        memcpy(&localRandom[1], smpLocalRandom_, sizeof(smpLocalRandom_));
        smpPairingState_ = kSmpPairingStateRandomSent;
        emitBleTrace("SMP_CONFIRM_RX");
        return buildSmpResponse(localRandom, sizeof(localRandom));
      }

      uint8_t localConfirm[17] = {0};
      localConfirm[0] = kSmpCodePairingConfirm;
      memcpy(&localConfirm[1], smpLocalConfirm_, sizeof(smpLocalConfirm_));
      smpPairingState_ = kSmpPairingStateConfirmSent;
      emitBleTrace("SMP_CONFIRM_RX");
      return buildSmpResponse(localConfirm, sizeof(localConfirm));
    }

    case kSmpCodePairingRandom: {
      if (smpLength != kSmpPairingRandomLen) {
        return buildSmpPairingFailed(kSmpReasonInvalidParameters);
      }
      if (!smpLocalInitiator_ &&
          smpSecureConnectionsActive_ &&
          smpSecureConnectionsRandomSent_ &&
          (smpPairingState_ == kSmpPairingStateRandomSent) &&
          (memcmp(smpPeerRandom_, &smp[1], sizeof(smpPeerRandom_)) == 0)) {
        uint8_t localRandom[17] = {0};
        localRandom[0] = kSmpCodePairingRandom;
        memcpy(&localRandom[1], smpLocalRandom_, sizeof(smpLocalRandom_));
        emitBleTrace("SMP_SC_RANDOM_RETRY");
        emitBleTrace("SMP_SC_RANDOM_TX");
        return buildSmpResponse(localRandom, sizeof(localRandom));
      }
      const bool expectingResponderRandom =
          smpLocalInitiator_ &&
          (smpPairingState_ == kSmpPairingStateRandomSent);
      const uint8_t incomingPairAlg =
          smpSecureConnectionsActive_
              ? resolveCurrentSecureConnectionsPairAlgorithm()
              : kSmpScPairAlgJustWorks;
      const bool expectingOobInitiatorRandom =
          !smpLocalInitiator_ && smpSecureConnectionsActive_ &&
          incomingPairAlg == kSmpScPairAlgOob &&
          smpPairingState_ == kSmpPairingStateRspSent &&
          smpSecureConnectionsPeerPublicKeyValid_ &&
          smpSecureConnectionsPublicKeySent_;
      if (smpPairingState_ != kSmpPairingStateConfirmSent &&
          !expectingResponderRandom && !expectingOobInitiatorRandom) {
        return buildSmpPairingFailed(kSmpReasonUnspecified);
      }

      memcpy(smpPeerRandom_, &smp[1], sizeof(smpPeerRandom_));
      if (smpSecureConnectionsActive_) {
        const uint8_t pairAlg = incomingPairAlg;
        const bool requireLocalConfirmSent =
            pairAlg != kSmpScPairAlgOob &&
            (!smpLocalInitiator_ ||
             secureConnectionsInitiatorTransmitsConfirm());
        if (requireLocalConfirmSent && !smpSecureConnectionsConfirmSent_) {
          return buildSmpPairingFailed(kSmpReasonUnspecified);
        }

        if (pairAlg != kSmpScPairAlgOob &&
            (smpLocalInitiator_ ||
             secureConnectionsResponderVerifiesRandom())) {
          uint8_t expectedRemoteConfirm[16] = {0};
          uint8_t confirmZ = 0U;
          if (pairAlg == kSmpScPairAlgPasskey) {
            if (prepareSecureConnectionsPasskey() !=
                BleSmpPasskeyPreparationResult::Ready) {
              return buildSmpPairingFailed(kSmpReasonAuthRequirements);
            }
            confirmZ = secureConnectionsPasskeyRoundZ();
          }
          if (!bleSecureConnectionsF4(&smpSecureConnectionsPeerPublicKey_[1],
                                      &smpSecureConnectionsPublicKey_[1],
                                      smpPeerRandom_, confirmZ,
                                      expectedRemoteConfirm)) {
            return buildSmpPairingFailed(kSmpReasonUnspecified);
          }
          if (memcmp(expectedRemoteConfirm, smpPeerConfirm_,
                     sizeof(smpPeerConfirm_)) != 0) {
            return buildSmpPairingFailed(kSmpReasonConfirmValueFailed);
          }
        }

        emitBleTrace("SMP_SC_RANDOM_RX");
        if (pairAlg == kSmpScPairAlgPasskey) {
          if (smpLocalInitiator_) {
            if (advanceSecureConnectionsPasskeyRound()) {
              if (!computeSecureConnectionsLocalConfirm()) {
                return buildSmpPairingFailed(kSmpReasonUnspecified);
              }
              uint8_t localConfirm[17] = {0};
              localConfirm[0] = kSmpCodePairingConfirm;
              memcpy(&localConfirm[1], smpLocalConfirm_,
                     sizeof(smpLocalConfirm_));
              smpSecureConnectionsConfirmSent_ = true;
              smpPairingState_ = kSmpPairingStateConfirmSent;
              emitBleTrace("SMP_SC_CONFIRM_TX");
              return buildSmpResponse(localConfirm, sizeof(localConfirm));
            }
            smpSecureConnectionsDeferredDhKeyCheck_ = true;
            return false;
          }

          if (smpSecureConnectionsRandomSent_) {
            smpPairingState_ = kSmpPairingStateRandomSent;
            return false;
          }

          uint8_t localRandom[17] = {0};
          localRandom[0] = kSmpCodePairingRandom;
          memcpy(&localRandom[1], smpLocalRandom_, sizeof(smpLocalRandom_));
          smpSecureConnectionsRandomSent_ = true;
          const bool hasMoreRounds = advanceSecureConnectionsPasskeyRound();
          if (!hasMoreRounds) {
            smpSecureConnectionsDeferredDhKeyCheck_ = true;
            smpPairingState_ = kSmpPairingStateRandomSent;
          } else {
            smpPairingState_ = kSmpPairingStateRspSent;
          }
          emitBleTrace("SMP_SC_RANDOM_TX");
          return buildSmpResponse(localRandom, sizeof(localRandom));
        }

        if (pairAlg == kSmpScPairAlgNumericComparison) {
          uint32_t passkey = 0UL;
          if (smpLocalInitiator_) {
            if (!bleSecureConnectionsG2(&smpSecureConnectionsPublicKey_[1],
                                        &smpSecureConnectionsPeerPublicKey_[1],
                                        smpLocalRandom_, smpPeerRandom_,
                                        &passkey)) {
              return buildSmpPairingFailed(kSmpReasonUnspecified);
            }
            smpPendingUserPasskeyValue_ = passkey;
            smpPendingUserPasskeyMatchRequest_ = true;
            smpPendingUserPasskey_ = true;
            smpPendingUserAction_ = BleSmpUserAction::NumericComparison;
            emitBleTrace("SMP_SC_NUMCOMP_PENDING");
            return false;
          }

          if (!bleSecureConnectionsG2(&smpSecureConnectionsPeerPublicKey_[1],
                                      &smpSecureConnectionsPublicKey_[1],
                                      smpPeerRandom_, smpLocalRandom_,
                                      &passkey)) {
            return buildSmpPairingFailed(kSmpReasonUnspecified);
          }
          smpPendingUserPasskeyValue_ = passkey;
          smpPendingUserPasskeyMatchRequest_ = true;
          smpPendingUserPasskey_ = true;
          smpPendingUserAction_ = BleSmpUserAction::NumericComparison;
          if (smpSecureConnectionsRandomSent_) {
            smpPairingState_ = kSmpPairingStateRandomSent;
            emitBleTrace("SMP_SC_NUMCOMP_PENDING");
            return false;
          }

          uint8_t localRandom[17] = {0};
          localRandom[0] = kSmpCodePairingRandom;
          memcpy(&localRandom[1], smpLocalRandom_, sizeof(smpLocalRandom_));
          smpSecureConnectionsRandomSent_ = true;
          smpPairingState_ = kSmpPairingStateRandomSent;
          emitBleTrace("SMP_SC_RANDOM_TX");
          emitBleTrace("SMP_SC_NUMCOMP_PENDING");
          return buildSmpResponse(localRandom, sizeof(localRandom));
        }
        if (smpLocalInitiator_) {
          smpSecureConnectionsDeferredDhKeyCheck_ = true;
          return false;
        }
        if (smpSecureConnectionsRandomSent_) {
          smpPairingState_ = kSmpPairingStateRandomSent;
          return false;
        }

        uint8_t localRandom[17] = {0};
        localRandom[0] = kSmpCodePairingRandom;
        memcpy(&localRandom[1], smpLocalRandom_, sizeof(smpLocalRandom_));
        smpSecureConnectionsRandomSent_ = true;
        smpSecureConnectionsDeferredDhKeyCheck_ = true;
        smpPairingState_ = kSmpPairingStateRandomSent;
        emitBleTrace("SMP_SC_RANDOM_TX");
        return buildSmpResponse(localRandom, sizeof(localRandom));
      }

      uint8_t tk[16] = {0};
      buildLegacyTemporaryKey(tk);
      const uint8_t iat = connectionPeerAddressRandom_ ? 0x01U : 0x00U;
      const uint8_t rat = bleAddressTypeIsRandom(addressType_) ? 0x01U : 0x00U;
      uint8_t expectedRemoteConfirm[16] = {0};
      if (!smpC1(tk, smpPeerRandom_, smpPairingReq_, smpPairingRsp_, iat,
                 connectionPeerAddress_, rat, address_, expectedRemoteConfirm)) {
        return buildSmpPairingFailed(kSmpReasonUnspecified);
      }

      if (memcmp(expectedRemoteConfirm, smpPeerConfirm_, sizeof(smpPeerConfirm_)) != 0) {
        return buildSmpPairingFailed(kSmpReasonConfirmValueFailed);
      }

      if (smpLocalInitiator_) {
        if (!smpS1(tk, smpPeerRandom_, smpLocalRandom_, smpStk_)) {
          return buildSmpPairingFailed(kSmpReasonUnspecified);
        }
        reduceSmpEncryptionKey(smpStk_);
        smpStkValid_ = true;
        connectionEncAuthenticated_ = currentPairingIsAuthenticated();
        connectionEncSecureConnections_ = false;
        memcpy(encDebug_.encLastStk, smpStk_, sizeof(encDebug_.encLastStk));
        emitBleTrace("SMP_RANDOM_RX");
        return false;
      }

      // STK = s1(TK, Srand, Mrand) per BLE spec Vol 3 Part H §2.2.4.
      // s1 r1=Srand (responder = local), r2=Mrand (initiator = peer).
      if (!smpS1(tk, smpLocalRandom_, smpPeerRandom_, smpStk_)) {
        return buildSmpPairingFailed(kSmpReasonUnspecified);
      }
      reduceSmpEncryptionKey(smpStk_);
      smpStkValid_ = true;
      connectionEncAuthenticated_ = currentPairingIsAuthenticated();
      connectionEncSecureConnections_ = false;
      memcpy(encDebug_.encLastStk, smpStk_, sizeof(encDebug_.encLastStk));

      uint8_t localRandom[17] = {0};
      localRandom[0] = kSmpCodePairingRandom;
      memcpy(&localRandom[1], smpLocalRandom_, sizeof(smpLocalRandom_));
      smpPairingState_ = kSmpPairingStateRandomSent;
      emitBleTrace("SMP_RANDOM_RX");
      return buildSmpResponse(localRandom, sizeof(localRandom));
    }

    case kSmpCodeDhKeyCheck: {
      if (smpLength != kSmpDhKeyCheckLen) {
        return buildSmpPairingFailed(kSmpReasonInvalidParameters);
      }
      if (!smpSecureConnectionsActive_ || !smpSecureConnectionsRandomSent_) {
        return buildSmpPairingFailed(kSmpReasonUnspecified);
      }
      memcpy(smpSecureConnectionsReceivedDhKeyCheck_, &smp[1],
             sizeof(smpSecureConnectionsReceivedDhKeyCheck_));
      smpSecureConnectionsReceivedDhKeyCheckValid_ = true;
      if (smpPendingUserPasskey_ && smpPendingUserPasskeyMatchRequest_ &&
          resolveCurrentSecureConnectionsPairAlgorithm() ==
              kSmpScPairAlgNumericComparison) {
        smpSecureConnectionsDeferredDhKeyCheck_ = true;
        emitBleTrace("SMP_SC_DHKEY_CHECK_WAIT_USER");
        return false;
      }
      if (!smpSecureConnectionsCheckValuesReady_) {
        smpSecureConnectionsDeferredDhKeyCheck_ = true;
        emitBleTrace("SMP_SC_DHKEY_CHECK_DEFERRED");
        return false;
      }
      if (memcmp(&smp[1], smpSecureConnectionsPeerDhKeyCheck_,
                 sizeof(smpSecureConnectionsPeerDhKeyCheck_)) != 0) {
        return buildSmpPairingFailed(kSmpReasonDhKeyCheckFailed);
      }
      if (smpLocalInitiator_) {
        smpSecureConnectionsDeferredDhKeyCheck_ = false;
        smpSecureConnectionsReceivedDhKeyCheckValid_ = false;
        invalidateSecureConnectionsKeypairAndOob(
            smpSecureConnectionsLocalKeyReady_,
            smpSecureConnectionsLocalKeyInProgress_,
            smpSecureConnectionsPrivateKey_, smpSecureConnectionsPublicKey_,
            smpOobDataLocalValid_, smpOobDataRemoteValid_, smpOobLocal_,
            smpOobRemote_);
        emitBleTrace("SMP_SC_DHKEY_CHECK_RX");
        return false;
      }

      uint8_t localDhKeyCheck[kSmpDhKeyCheckLen] = {0};
      localDhKeyCheck[0] = kSmpCodeDhKeyCheck;
      memcpy(&localDhKeyCheck[1], smpSecureConnectionsLocalDhKeyCheck_,
             sizeof(smpSecureConnectionsLocalDhKeyCheck_));
      smpSecureConnectionsDeferredDhKeyCheck_ = false;
      smpSecureConnectionsReceivedDhKeyCheckValid_ = false;
      smpSecureConnectionsDhKeyCheckSent_ = true;
      invalidateSecureConnectionsKeypairAndOob(
          smpSecureConnectionsLocalKeyReady_,
          smpSecureConnectionsLocalKeyInProgress_,
          smpSecureConnectionsPrivateKey_, smpSecureConnectionsPublicKey_,
          smpOobDataLocalValid_, smpOobDataRemoteValid_, smpOobLocal_,
          smpOobRemote_);
      emitBleTrace("SMP_SC_DHKEY_CHECK_RX");
      emitBleTrace("SMP_SC_DHKEY_CHECK_TX");
      return buildSmpResponse(localDhKeyCheck, sizeof(localDhKeyCheck));
    }

    case kSmpCodeEncryptionInformation:
      if (smpLength != kSmpEncryptionInformationLen) {
        return buildSmpPairingFailed(kSmpReasonInvalidParameters);
      }
      if (!smpBondingRequested_ || smpSecureConnectionsActive_ ||
          !smpExpectPeerEncKey_ ||
          !connectionEncSessionValid_ || !connectionEncRxEnabled_ ||
          !connectionEncTxEnabled_) {
        return buildSmpPairingFailed(kSmpReasonInvalidParameters);
      }
      if (smpPeerLtkValid_) {
        uint8_t repeatedLtk[16] = {0};
        memcpy(repeatedLtk, &smp[1], sizeof(repeatedLtk));
        reduceSmpEncryptionKey(repeatedLtk);
        if (memcmp(repeatedLtk, smpPeerLtk_, sizeof(repeatedLtk)) == 0) {
          emitBleTrace("SMP_ENC_INFO_RETRY");
          return false;
        }
        return buildSmpPairingFailed(kSmpReasonInvalidParameters);
      }
      memcpy(smpPeerLtk_, &smp[1], sizeof(smpPeerLtk_));
      reduceSmpEncryptionKey(smpPeerLtk_);
      smpPeerLtkValid_ = true;
      smpPeerLtkAwaitMasterId_ = true;
      emitBleTrace("SMP_ENC_INFO_RX");
      return false;

    case kSmpCodeMasterIdentification:
      if (smpLength != kSmpMasterIdentificationLen) {
        return buildSmpPairingFailed(kSmpReasonInvalidParameters);
      }
      if (!smpBondingRequested_ || smpSecureConnectionsActive_ ||
          !smpExpectPeerEncKey_ || !smpPeerLtkValid_ ||
          !connectionEncSessionValid_ ||
          !connectionEncRxEnabled_ || !connectionEncTxEnabled_) {
        return buildSmpPairingFailed(kSmpReasonInvalidParameters);
      }
      if (!smpPeerLtkAwaitMasterId_) {
        if (smpEncReqEdiv_ == readLe16(&smp[1]) &&
            memcmp(smpEncReqRand_, &smp[3], sizeof(smpEncReqRand_)) == 0) {
          emitBleTrace("SMP_MASTER_ID_RETRY");
          return false;
        }
        return buildSmpPairingFailed(kSmpReasonInvalidParameters);
      }
      smpEncReqEdiv_ = readLe16(&smp[1]);
      memcpy(smpEncReqRand_, &smp[3], sizeof(smpEncReqRand_));
      smpPeerLtkAwaitMasterId_ = false;
      emitBleTrace("SMP_MASTER_ID_RX");
      (void)completeSmpKeyDistributionIfDone();
      return false;

    case kSmpCodeIdentityInformation:
      if (smpLength != kSmpIdentityInformationLen) {
        return buildSmpPairingFailed(kSmpReasonInvalidParameters);
      }
      if (!smpBondingRequested_ || !smpExpectPeerIdKey_ ||
          (smpExpectPeerEncKey_ &&
           (!smpPeerLtkValid_ || smpPeerLtkAwaitMasterId_)) ||
          !connectionEncSessionValid_ || !connectionEncRxEnabled_ ||
          !connectionEncTxEnabled_) {
        return buildSmpPairingFailed(kSmpReasonInvalidParameters);
      }
      if (smpPeerIrkValid_) {
        if (memcmp(smpPeerIrk_, &smp[1], sizeof(smpPeerIrk_)) == 0) {
          emitBleTrace("SMP_ID_INFO_RETRY");
          return false;
        }
        return buildSmpPairingFailed(kSmpReasonInvalidParameters);
      }
      memcpy(smpPeerIrk_, &smp[1], sizeof(smpPeerIrk_));
      smpPeerIrkValid_ = true;
      emitBleTrace("SMP_ID_INFO_RX");
      (void)completeSmpKeyDistributionIfDone();
      return false;

    case kSmpCodeIdentityAddressInformation:
      if (smpLength != kSmpIdentityAddressInformationLen) {
        return buildSmpPairingFailed(kSmpReasonInvalidParameters);
      }
      if (!smpBondingRequested_ || !smpExpectPeerIdKey_ ||
          !smpPeerIrkValid_ ||
          !connectionEncSessionValid_ || !connectionEncRxEnabled_ ||
          !connectionEncTxEnabled_) {
        emitBleTrace("SMP_ID_ADDR_STATE_INVALID");
        return buildSmpPairingFailed(kSmpReasonInvalidParameters);
      }
      if (smp[1] > 0x01U) {
        emitBleTrace("SMP_ID_ADDR_TYPE_INVALID");
        return buildSmpPairingFailed(kSmpReasonInvalidParameters);
      }
      if (smp[1] == 0x01U && (smp[7] & 0xC0U) != 0xC0U) {
        emitBleTrace("SMP_ID_ADDR_STATIC_INVALID");
        return buildSmpPairingFailed(kSmpReasonInvalidParameters);
      }
      if (smpPeerIdentityAddressValid_) {
        if (smpPeerIdentityAddressRandom_ == (smp[1] != 0U) &&
            memcmp(smpPeerIdentityAddress_, &smp[2],
                   sizeof(smpPeerIdentityAddress_)) == 0) {
          emitBleTrace("SMP_ID_ADDR_RETRY");
          return false;
        }
        return buildSmpPairingFailed(kSmpReasonInvalidParameters);
      }
      memcpy(smpPeerIdentityAddress_, &smp[2], sizeof(smpPeerIdentityAddress_));
      smpPeerIdentityAddressRandom_ = (smp[1] != 0U);
      smpPeerIdentityAddressValid_ = true;
      emitBleTrace("SMP_ID_ADDR_RX");
      (void)completeSmpKeyDistributionIfDone();
      return false;

    case kSmpCodeSigningInformation:
      if (smpLength != kSmpSigningInformationLen) {
        return buildSmpPairingFailed(kSmpReasonInvalidParameters);
      }
      if (!smpBondingRequested_ || !smpExpectPeerSignKey_ ||
          (smpExpectPeerEncKey_ &&
           (!smpPeerLtkValid_ || smpPeerLtkAwaitMasterId_)) ||
          (smpExpectPeerIdKey_ &&
           (!smpPeerIrkValid_ || !smpPeerIdentityAddressValid_)) ||
          !connectionEncSessionValid_ || !connectionEncRxEnabled_ ||
          !connectionEncTxEnabled_) {
        return buildSmpPairingFailed(kSmpReasonInvalidParameters);
      }
      if (smpPeerCsrkValid_) {
        if (memcmp(smpPeerCsrk_, &smp[1], sizeof(smpPeerCsrk_)) == 0) {
          emitBleTrace("SMP_SIGN_INFO_RETRY");
          return false;
        }
        return buildSmpPairingFailed(kSmpReasonInvalidParameters);
      }
      memcpy(smpPeerCsrk_, &smp[1], sizeof(smpPeerCsrk_));
      smpPeerCsrkValid_ = true;
      smpPeerSignCounter_ = 0U;
      smpPeerSignCounterValid_ = false;
      emitBleTrace("SMP_SIGN_INFO_RX");
      (void)completeSmpKeyDistributionIfDone();
      return false;

    case kSmpCodePairingResponse:
      if (smpLength != kSmpPairingResponseLen) {
        return buildSmpPairingFailed(kSmpReasonInvalidParameters);
      }
      if (smp[1] > kSmpIoCapKeyboardDisplay || smp[2] > 0x01U ||
          (smp[3] & kSmpAuthReqBondingMask) > 0x01U ||
          (smp[3] & 0xC0U) != 0U) {
        return buildSmpPairingFailed(kSmpReasonInvalidParameters);
      }
      if (smp[4] < 7U || smp[4] > 16U) {
        return buildSmpPairingFailed(kSmpReasonEncryptionKeySize);
      }
      if (!smpLocalInitiator_) {
        return buildSmpPairingFailed(kSmpReasonCommandNotSupported);
      }
      if ((smp[5] & static_cast<uint8_t>(~kSmpKeyDistDefinedMask)) != 0U ||
          (smp[6] & static_cast<uint8_t>(~kSmpKeyDistDefinedMask)) != 0U ||
          (smp[5] & kSmpKeyDistSupportedMask &
           static_cast<uint8_t>(~smpPairingReq_[5])) != 0U ||
          (smp[6] & kSmpKeyDistSupportedMask &
           static_cast<uint8_t>(~smpPairingReq_[6])) != 0U) {
        return buildSmpPairingFailed(kSmpReasonInvalidParameters);
      }
      if (smpPairingState_ != kSmpPairingStateReqSent) {
        if (memcmp(smpPairingRsp_, smp, kSmpPairingResponseLen) != 0) {
          return buildSmpPairingFailed(kSmpReasonCommandNotSupported);
        }
        if (smpSecureConnectionsActive_) {
          if (smpSecureConnectionsPublicKeySent_) {
            return false;
          }
          smpSecureConnectionsWireFormatBigEndian_ = false;
          if (!smpSecureConnectionsLocalKeyReady_) {
            smpSecureConnectionsDeferredPublicKey_ = true;
            return false;
          }
          if (buildLocalSecureConnectionsPublicKeyResponse()) {
            return true;
          }
          smpSecureConnectionsDeferredPublicKey_ = true;
          return false;
        }
        if (smpPairingState_ == kSmpPairingStateRandomSent) {
          return false;
        }
        if (smpLegacyConfirmDeferred_) {
          return false;
        }
        uint8_t localConfirm[17] = {0};
        localConfirm[0] = kSmpCodePairingConfirm;
        memcpy(&localConfirm[1], smpLocalConfirm_, sizeof(smpLocalConfirm_));
        return buildSmpResponse(localConfirm, sizeof(localConfirm));
      }
      memcpy(smpPairingRsp_, smp, kSmpPairingResponseLen);
      // LinkKey is a defined CTKD request bit, but this LE-only host does not
      // implement CTKD. Preserve the response verbatim for c1 and duplicate
      // PDU checks; the negotiated state below selects only supported bits.
      smpPairingFailureReasonPending_ = false;
      smpLastPairingFailureReason_ = 0U;
      smpBondingRequested_ =
          ((smpPairingReq_[3] & kSmpAuthReqBondingMask) != 0U) &&
          ((smpPairingRsp_[3] & kSmpAuthReqBondingMask) != 0U);
      smpSecureConnectionsActive_ =
          ((smpPairingReq_[3] & kSmpAuthReqSecureConnectionsMask) != 0U) &&
          ((smpPairingRsp_[3] & kSmpAuthReqSecureConnectionsMask) != 0U);
      if (smpSecurityPolicy_.secureConnectionsRequired &&
          !smpSecureConnectionsActive_) {
        return buildSmpPairingFailed(kSmpReasonAuthRequirements);
      }
      if (!smpSecureConnectionsActive_ && smpOobEnabled_ &&
          (smpOobDataLocalValid_ || smpOobDataRemoteValid_)) {
        return buildSmpPairingFailed(kSmpReasonOobNotAvailable);
      }
      smpKeySize_ =
          (smpPairingRsp_[4] < smpPairingReq_[4]) ? smpPairingRsp_[4]
                                                  : smpPairingReq_[4];
      if (smpKeySize_ < smpSecurityPolicy_.minEncryptionKeySize ||
          smpKeySize_ > smpSecurityPolicy_.maxEncryptionKeySize) {
        return buildSmpPairingFailed(kSmpReasonEncryptionKeySize);
      }
      smpExpectPeerEncKey_ = smpSecureConnectionsActive_
                                      ? false
                                      : ((smpPairingRsp_[6] &
                                          kSmpKeyDistEncKeyMask) != 0U);
      smpExpectPeerIdKey_ =
          ((smpPairingRsp_[6] & kSmpKeyDistIdKeyMask) != 0U);
      smpExpectPeerSignKey_ =
          ((smpPairingRsp_[6] & kSmpKeyDistSignKeyMask) != 0U);
      smpDistributeLocalEncKey_ = !smpSecureConnectionsActive_ &&
          ((smpPairingRsp_[5] & kSmpKeyDistEncKeyMask) != 0U);
      smpDistributeLocalIdKey_ =
          ((smpPairingRsp_[5] & kSmpKeyDistIdKeyMask) != 0U);
      smpDistributeLocalSignKey_ =
          ((smpPairingRsp_[5] & kSmpKeyDistSignKeyMask) != 0U);
      if (smpSecureConnectionsActive_) {
        smpExpectPeerEncKey_ = false;
        smpDistributeLocalEncKey_ = false;
      }
      if (smpDistributeLocalEncKey_ &&
          !prepareLocalLegacyEncryptionKey()) {
        return buildSmpPairingFailed(kSmpReasonUnspecified);
      }
      smpLocalEncInfoSent_ = false;
      smpLocalEncInfoAcked_ = false;
      smpLocalMasterIdSent_ = false;
      smpLocalMasterIdAcked_ = false;
      smpLocalIdInfoSent_ = false;
      smpLocalIdInfoAcked_ = false;
      smpLocalIdAddressSent_ = false;
      smpLocalIdAddressAcked_ = false;
      smpLocalSignInfoSent_ = false;
      smpLocalSignInfoAcked_ = false;
      if (!prepareLocalPairingRandom()) {
        return buildSmpPairingFailed(kSmpReasonUnspecified);
      }
      smpPairingState_ = kSmpPairingStateRspSent;
      emitBleTrace("SMP_PAIRING_RESPONSE_RX");
      if (smpSecureConnectionsActive_) {
        const uint8_t pairAlg = resolveCurrentSecureConnectionsPairAlgorithm();
        if ((smpPairingReq_[3] & kSmpAuthReqMitmMask) != 0U &&
            pairAlg == kSmpScPairAlgJustWorks) {
          emitBleTrace("SMP_SC_AUTH_REQUIREMENTS");
          return buildSmpPairingFailed(kSmpReasonAuthRequirements);
        }
        if (smpOobEnabled_ &&
            (smpOobDataLocalValid_ || smpOobDataRemoteValid_) &&
            pairAlg != kSmpScPairAlgOob) {
          emitBleTrace("SMP_SC_OOB_INCONSISTENT");
          return buildSmpPairingFailed(kSmpReasonOobNotAvailable);
        }
        if (!secureConnectionsPairAlgorithmSupported(pairAlg)) {
          emitBleTrace("SMP_SC_ALG_UNSUPPORTED");
          return buildSmpPairingFailed(
              (pairAlg == kSmpScPairAlgOob) ? kSmpReasonOobNotAvailable
                                            : kSmpReasonAuthRequirements);
        }
        smpSecureConnectionsWireFormatBigEndian_ = false;
        smpSecureConnectionsDeferredPublicKey_ = true;
        return false;
      }

      {
      const uint8_t legacyAction = resolveLegacyPairingIoAction();
      if (smpSecurityPolicy_.mitmRequired &&
          legacyAction == kSmpScIoActionNone) {
        emitBleTrace("SMP_LEGACY_AUTH_REQUIREMENTS");
        return buildSmpPairingFailed(kSmpReasonAuthRequirements);
      }
      const BleSmpPasskeyPreparationResult passkeyResult =
          preparePairingPasskeyForAction(legacyAction);
      if (passkeyResult == BleSmpPasskeyPreparationResult::Fatal) {
        return buildSmpPairingFailed(kSmpReasonAuthRequirements);
      }
      if (passkeyResult == BleSmpPasskeyPreparationResult::Pending) {
        smpLegacyConfirmDeferred_ = true;
        return false;
      }
        uint8_t tk[16] = {0};
        buildLegacyTemporaryKey(tk);
        const uint8_t iat = bleAddressTypeIsRandom(addressType_) ? 0x01U : 0x00U;
        const uint8_t rat = connectionPeerAddressRandom_ ? 0x01U : 0x00U;
        if (!smpC1(tk, smpLocalRandom_, smpPairingReq_, smpPairingRsp_, iat,
                   address_, rat, connectionPeerAddress_, smpLocalConfirm_)) {
          return buildSmpPairingFailed(kSmpReasonUnspecified);
        }
        uint8_t localConfirm[17] = {0};
        localConfirm[0] = kSmpCodePairingConfirm;
        memcpy(&localConfirm[1], smpLocalConfirm_, sizeof(smpLocalConfirm_));
        smpPairingState_ = kSmpPairingStateConfirmSent;
        emitBleTrace("SMP_CONFIRM_TX");
        return buildSmpResponse(localConfirm, sizeof(localConfirm));
      }

    case kSmpCodeSecurityRequest: {
      if (smpLength != 2U) {
        return buildSmpPairingFailed(kSmpReasonInvalidParameters);
      }
      const uint8_t peerAuthReq = smp[1];
      if ((peerAuthReq & kSmpAuthReqBondingMask) > 0x01U ||
          (peerAuthReq & 0xC0U) != 0U) {
        return buildSmpPairingFailed(kSmpReasonInvalidParameters);
      }
      const bool storedBondMeetsMitm =
          (peerAuthReq & kSmpAuthReqMitmMask) == 0U ||
          connectionEncAuthenticated_;
      const bool storedBondMeetsSc =
          (peerAuthReq & kSmpAuthReqSecureConnectionsMask) == 0U ||
          ((bondRecord_.reserved[0] &
            kBleBondRecordFlagSecureConnections) != 0U);
      activeBondMeetsSecurityPolicy_ =
          bondKeyPrimedForConnection_ &&
          bondMeetsSecurityPolicy(bondRecord_, securityPolicy_);
      if (connectionRole_ == BleConnectionRole::kCentral &&
          (connectionEncSessionValid_ || connectionEncRxEnabled_ ||
           connectionEncTxEnabled_)) {
        emitBleTrace("SMP_SECURITY_REQUEST_RX_ENCRYPTED");
        return buildSmpPairingFailed(kSmpReasonPairingNotSupported);
      }
      if ((connectionRole_ == BleConnectionRole::kCentral) && smpStkValid_ &&
          activeBondMeetsSecurityPolicy_ && storedBondMeetsMitm &&
          storedBondMeetsSc) {
        emitBleTrace("SMP_SECURITY_REQUEST_RX_BONDED");
        connectionBondedEncryptionRequested_ = true;
        if (!connectionEncSessionValid_ &&
            !connectionEncRxEnabled_ &&
            !connectionEncTxEnabled_ &&
            !connectionEncKeyDerivationPending_ &&
            !connectionEncStartReqPending_ &&
            !connectionEncStartReqTxPending_ &&
            !connectionEncAwaitingStartRsp_) {
          maybeQueueCentralLinkSetupRequest();
        }
        return false;
      }
      if ((connectionRole_ == BleConnectionRole::kCentral) && smpStkValid_) {
        clearSmpPairingState();
        connectionEncAuthenticated_ = false;
        connectionBondedEncryptionRequested_ = false;
        emitBleTrace("SMP_SECURITY_REQUEST_BOND_UPGRADE");
      }
      if (smpPairingState_ == kSmpPairingStateIdle) {
        latchSecurityPolicyForPairing();
        if (smpSecurityPolicy_.bondingEnabled &&
            !smpSecurityPolicy_.secureConnectionsRequired) {
          (void)prepareLocalLegacyEncryptionKey();
        }
        uint8_t pairingReq[kSmpPairingRequestLen] = {0};
        const bool peerSupportsSecureConnections =
            (peerAuthReq & kSmpAuthReqSecureConnectionsMask) != 0U;
        if (smpOobEnabled_ && !smpOobDataLocalValid_ &&
            !smpOobDataRemoteValid_) {
          return buildSmpPairingFailed(kSmpReasonOobNotAvailable);
        }
        const bool localHasPeerOobData =
            smpOobEnabled_ && smpOobDataRemoteValid_ &&
            peerSupportsSecureConnections;
        const bool localOobConfigured =
            smpOobEnabled_ &&
            (smpOobDataLocalValid_ || smpOobDataRemoteValid_);
        if (localOobConfigured && !peerSupportsSecureConnections) {
          return buildSmpPairingFailed(kSmpReasonOobNotAvailable);
        }
        pairingReq[0] = kSmpCodePairingRequest;
        pairingReq[1] = smpPairingLocalIoCapabilities_;
        pairingReq[2] = localHasPeerOobData ? 0x01U : 0x00U;
        pairingReq[3] = buildLocalPairingAuthReq(true);
        if ((peerAuthReq & kSmpAuthReqMitmMask) != 0U) {
          pairingReq[3] |= kSmpAuthReqMitmMask;
        }
        if (smpSecurityPolicy_.secureConnectionsEnabled) {
          // SC does not distribute EncKey. Exchange both identity keys so
          // either role can resolve a bonded peer after its RPA rotates.
          const uint8_t peerKeys =
              ((pairingReq[3] & kSmpAuthReqBondingMask) != 0U)
                  ? static_cast<uint8_t>(kSmpKeyDistEncKeyMask |
                                         kSmpKeyDistIdKeyMask |
                                         kSmpKeyDistSignKeyMask)
                  : 0U;
          pairingReq[5] =
              ((pairingReq[3] & kSmpAuthReqBondingMask) != 0U)
                  ? kSmpKeyDistSignKeyMask
                  : 0U;
          if (localIdentityDistributionAvailable()) {
            pairingReq[5] |= kSmpKeyDistIdKeyMask;
          }
          if (smpLocalLtkValid_) {
            pairingReq[5] |= kSmpKeyDistEncKeyMask;
          }
          pairingReq[6] = peerKeys;
        } else {
          if ((pairingReq[3] & kSmpAuthReqBondingMask) != 0U) {
            pairingReq[5] = localIdentityDistributionAvailable()
                                ? kSmpKeyDistIdKeyMask
                                : 0U;
            pairingReq[5] |= kSmpKeyDistSignKeyMask;
            if (smpLocalLtkValid_) {
              pairingReq[5] |= kSmpKeyDistEncKeyMask;
            }
            pairingReq[6] = static_cast<uint8_t>(
                kSmpKeyDistEncKeyMask | kSmpKeyDistIdKeyMask |
                kSmpKeyDistSignKeyMask);
          } else {
            pairingReq[5] = 0U;
            pairingReq[6] = 0U;
          }
        }
        pairingReq[4] = smpSecurityPolicy_.maxEncryptionKeySize;
        memcpy(smpPairingReq_, pairingReq, sizeof(smpPairingReq_));
        smpLocalInitiator_ = true;
        smpBondingRequested_ =
            ((pairingReq[3] & kSmpAuthReqBondingMask) != 0U);
        smpSecureConnectionsActive_ =
            ((pairingReq[3] & kSmpAuthReqSecureConnectionsMask) != 0U);
        smpPairingState_ = kSmpPairingStateReqSent;
        emitBleTrace("SMP_SECURITY_REQUEST_RX");
        emitBleTrace("SMP_PAIRING_REQUEST_TX");
        return buildSmpResponse(pairingReq, kSmpPairingRequestLen);
      }
      return false;
    }

    default:
      if (smpCode == kSmpCodeKeypressNotification) {
        return buildSmpPairingFailed(kSmpReasonCommandNotSupported);
      }
      emitBleTrace("SMP_RESERVED_CODE_IGNORED");
      return false;
  }
}

bool BleRadio::buildL2capAttResponse(const uint8_t* l2capPayload,
                                     uint8_t l2capPayloadLength,
                                     uint8_t* outPayload,
                                     uint8_t* outPayloadLength) {
  if (l2capPayload == nullptr || outPayload == nullptr || outPayloadLength == nullptr ||
      l2capPayloadLength < kBleL2capHeaderLen) {
    return false;
  }

  const uint16_t declaredL2capLength = readLe16(&l2capPayload[0]);
  const uint16_t cid = readLe16(&l2capPayload[2]);
  if (cid != kBleL2capCidAtt) {
    return false;
  }

  const uint16_t available = static_cast<uint16_t>(l2capPayloadLength - kBleL2capHeaderLen);
  const uint16_t attRequestLength = minU16(declaredL2capLength, available);
  if (attRequestLength == 0U) {
    return false;
  }

  g_ble_periph_last_att_req_opcode = l2capPayload[kBleL2capHeaderLen];
  ++g_ble_periph_att_req_count;

  uint16_t attResponseLength = 0U;
  if (!buildAttResponse(&l2capPayload[kBleL2capHeaderLen], attRequestLength,
                        &outPayload[kBleL2capHeaderLen], &attResponseLength) ||
      attResponseLength == 0U) {
    return false;
  }

  if (attResponseLength > currentMaxAttPayloadLength()) {
    return false;
  }

  writeLe16(&outPayload[0], attResponseLength);
  writeLe16(&outPayload[2], kBleL2capCidAtt);
  g_ble_periph_last_att_rsp_opcode = outPayload[kBleL2capHeaderLen];
  ++g_ble_periph_att_rsp_count;
  *outPayloadLength =
      static_cast<uint8_t>(kBleL2capHeaderLen + static_cast<uint8_t>(attResponseLength));
  return true;
}
