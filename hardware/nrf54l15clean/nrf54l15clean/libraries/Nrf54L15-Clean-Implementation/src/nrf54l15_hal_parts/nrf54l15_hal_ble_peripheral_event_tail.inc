	            uint8_t txFollowLengthOnAir = txFollowLength;
	            uint64_t txFollowCounterUsed = 0ULL;
	            bool txFollowCounterValid = false;
	            bool txFollowEncrypted = false;
	            const uint8_t* const txFollowPlainPayload =
	                txFollowCanUseFreshPayload
	                    ? &connectionTxPayload_[0]
	                    : &connectionLastTxPlainPayload_[0];
	            const bool txFollowIsStartEncRspPlain =
	                (txFollowLlid == kBlePduLlControl) &&
	                (txFollowLength >= 1U) &&
	                (txFollowPlainPayload[0] == kBleLlCtrlStartEncRsp);
	            if (!prepareFollowTxPacket(txFollowLlid, txFollowLength,
	                                       txFollowPlainPayload,
	                                       txFollowCanUseFreshPayload,
	                                       txFollowHasMoreData,
	                                       &txFollowLengthOnAir,
	                                       &txFollowCounterUsed,
	                                       &txFollowCounterValid,
	                                       &txFollowEncrypted)) {
	              break;
	            }

	            clearRadioCoreEvents(radio_);
	            {
	              const size_t txCopyLen = 2U + static_cast<size_t>(txFollowLengthOnAir);
	              const size_t copyLen =
	                  (txCopyLen <= sizeof(rxPacket_)) ? txCopyLen : sizeof(rxPacket_);
	              memcpy(rxPacket_, txPacket_, copyLen);
	            }
            radio_->PACKETPTR =
                static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&rxPacket_[0]));
            radio_->SHORTS =
                ((RADIO_SHORTS_TXREADY_START_Enabled << RADIO_SHORTS_TXREADY_START_Pos) &
                 RADIO_SHORTS_TXREADY_START_Msk) |
                ((RADIO_SHORTS_PHYEND_DISABLE_Enabled << RADIO_SHORTS_PHYEND_DISABLE_Pos) &
                 RADIO_SHORTS_PHYEND_DISABLE_Msk) |
                ((RADIO_SHORTS_DISABLED_RXEN_Enabled << RADIO_SHORTS_DISABLED_RXEN_Pos) &
                 RADIO_SHORTS_DISABLED_RXEN_Msk) |
                ((RADIO_SHORTS_RXREADY_START_Enabled << RADIO_SHORTS_RXREADY_START_Pos) &
                 RADIO_SHORTS_RXREADY_START_Msk) |
                ((RADIO_SHORTS_ADDRESS_RSSISTART_Enabled
                  << RADIO_SHORTS_ADDRESS_RSSISTART_Pos) &
                 RADIO_SHORTS_ADDRESS_RSSISTART_Msk);

            const uint32_t txFollowTriggerTargetUs =
                followRxEndTimestampUs + kBleConnTxenAfterRxUs;
            uint32_t txFollowTriggerNowUs = bleTimingUs();
            while (!timeReachedUs(txFollowTriggerNowUs, txFollowTriggerTargetUs)) {
              txFollowTriggerNowUs = bleTimingUs();
            }
            bleTriggerTxAtTargetUsCritical(radio_, txFollowTriggerTargetUs);

	            const uint32_t txFollowEndBudgetUs =
	                bleConnectionTxEndBudgetUsOnPhy(txFollowLengthOnAir,
	                                                connectionCurrentTxPhy_);
            const bool txFollowEndSeen =
                waitRadioEndBudgeted(radio_, txFollowEndBudgetUs, spinLimit / 2U + 1U);
            bool txFollowOk = false;
            if (txFollowEndSeen) {
              radio_->EVENTS_END = 0U;
              radio_->EVENTS_PHYEND = 0U;
              txFollowOk =
                  waitRadioDisabledBudgeted(radio_, kBleConnTxDisableWaitUs,
                                            spinLimit / 2U + 1U);
            }
            if (!txFollowOk) {
              radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
              waitRadioDisabledBudgeted(radio_, kBleConnTxDisableWaitUs,
                                        spinLimit / 2U + 1U);
              radio_->SHORTS = 0U;
            }

	            const bool preserveFailedFollowTx =
	                !txFollowOk && txFollowCanUseFreshPayload;
	            if (txFollowOk || preserveFailedFollowTx) {
	              connectionTxHistoryValid_ = true;
	              connectionLastTxMoreData_ =
	                  (txPacket_[0] & 0x10U) != 0U;
	              connectionLastTxSn_ =
	                  static_cast<uint8_t>((txPacket_[0] >> 3U) & 0x01U);
	              if (txFollowCanUseFreshPayload) {
	                connectionFreshTxAllowed_ = false;
	              }
	            }
	            if ((txFollowOk || preserveFailedFollowTx) &&
	                txFollowEncrypted && txFollowCounterValid) {
	              ++encDebug_.encTxPacketCount;
	              if (txFollowOk && txFollowIsStartEncRspPlain) {
	                ++encDebug_.followupStartEncRspTxOk;
	              }
	            }
	            if (!txFollowOk) {
	              if (preserveFailedFollowTx) {
	                emitBleTrace("FOLLOW_TX_TIMEOUT_RETAINED");
	              }
	              break;
	            }
	            radio_->EVENTS_DISABLED = 0U;
            radio_->EVENTS_CRCOK = 0U;
            radio_->EVENTS_CRCERROR = 0U;
            radio_->SHORTS =
                ((RADIO_SHORTS_RXREADY_START_Enabled << RADIO_SHORTS_RXREADY_START_Pos) &
                 RADIO_SHORTS_RXREADY_START_Msk) |
                ((RADIO_SHORTS_ADDRESS_RSSISTART_Enabled
                  << RADIO_SHORTS_ADDRESS_RSSISTART_Pos) &
                 RADIO_SHORTS_ADDRESS_RSSISTART_Msk) |
                ((RADIO_SHORTS_PHYEND_DISABLE_Enabled
                  << RADIO_SHORTS_PHYEND_DISABLE_Pos) &
                 RADIO_SHORTS_PHYEND_DISABLE_Msk);
            {
              const uint32_t state =
                  (radio_->STATE & RADIO_STATE_STATE_Msk) >> RADIO_STATE_STATE_Pos;
              if (state == RADIO_STATE_STATE_Disabled) {
                radio_->TASKS_RXEN = RADIO_TASKS_RXEN_TASKS_RXEN_Trigger;
              }
            }
            continue;
          }
        }
      }

      // Re-arm RX for another follow-up packet in the same event.
      clearRadioCoreEvents(radio_);
      radio_->PACKETPTR =
          static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&rxPacket_[0]));
      radio_->SHORTS = bleRadioShortsRxListen();
      radio_->TASKS_RXEN = RADIO_TASKS_RXEN_TASKS_RXEN_Trigger;
    }

    clearRadioCoreEvents(radio_);
  }
  if (restoreOriginalPacketAfterFollowup) {
    rxPacket_[0] = hdr0;
    rxPacket_[1] = rxLengthRaw;
    memcpy(&rxPacket_[2], postTxOriginalPayload, rxLengthRaw);
  }

  // Fallback in case TX path did not run to completion in this event.
  derivePendingEncSessionKey();

  // Awaiting LL_START_ENC_RSP is latency-critical for the ACK path: transmit the
  // ACK first, then validate/decode the encrypted control PDU.
  if (!terminateInd &&
      deferAwaitingStartRspDecrypt &&
      !fastStartEncRspMatched &&
      packetIsNew &&
      connectionEncSessionValid_ &&
      connectionEncAwaitingStartRsp_) {
    uint8_t plaintext[kBleDataPduMaxPayload] = {0};
    uint8_t plaintextLen = 0U;
    const uint64_t rxCounter = connectionEncRxCounter_;
    bool decryptOk = bleCcmDecryptPayload(connectionEncSessionKey_, connectionEncIv_,
                                          rxCounter, connectionEncRxDirection_, hdr0,
                                          &rxPacket_[2], rxLengthRaw,
                                          plaintext, &plaintextLen);
    if (!decryptOk) {
      const uint8_t swappedRxDirection =
          static_cast<uint8_t>(connectionEncRxDirection_ ^ 0x01U);
      if (bleCcmDecryptPayload(connectionEncSessionKey_, connectionEncIv_,
                               rxCounter, swappedRxDirection, hdr0,
                               &rxPacket_[2], rxLengthRaw,
                               plaintext, &plaintextLen)) {
        decryptOk = true;
        connectionEncRxDirection_ = swappedRxDirection;
        connectionEncTxDirection_ = static_cast<uint8_t>(swappedRxDirection ^ 0x01U);
        connectionEncPrecomputedStartRspValid_ = false;
        connectionEncPrecomputedStartRspTxValid_ = false;
        encDebug_.encLastRxDir = connectionEncRxDirection_;
        encDebug_.encLastTxDir = connectionEncTxDirection_;
        emitBleTrace("ENC_DIR_SWAP");
      } else if (connectionEncAltKeyValid_ &&
                 bleCcmDecryptPayload(connectionEncSessionKeyAlt_, connectionEncIv_,
                                      rxCounter, connectionEncRxDirection_, hdr0,
                                      &rxPacket_[2], rxLengthRaw,
                                      plaintext, &plaintextLen)) {
        decryptOk = true;
        memcpy(connectionEncSessionKey_, connectionEncSessionKeyAlt_,
               sizeof(connectionEncSessionKey_));
        connectionEncAltKeyValid_ = false;
        connectionEncPrecomputedStartRspValid_ = false;
        connectionEncPrecomputedStartRspTxValid_ = false;
        memcpy(encDebug_.encLastSessionKey, connectionEncSessionKey_,
               sizeof(encDebug_.encLastSessionKey));
        encDebug_.encLastSessionKeyValid = 1U;
        emitBleTrace("ENC_KEY_SWAP");
      } else if (connectionEncAltKeyValid_ &&
                 bleCcmDecryptPayload(connectionEncSessionKeyAlt_, connectionEncIv_,
                                      rxCounter, swappedRxDirection, hdr0,
                                      &rxPacket_[2], rxLengthRaw,
                                      plaintext, &plaintextLen)) {
        decryptOk = true;
        memcpy(connectionEncSessionKey_, connectionEncSessionKeyAlt_,
               sizeof(connectionEncSessionKey_));
        connectionEncAltKeyValid_ = false;
        connectionEncRxDirection_ = swappedRxDirection;
        connectionEncTxDirection_ = static_cast<uint8_t>(swappedRxDirection ^ 0x01U);
        connectionEncPrecomputedStartRspValid_ = false;
        connectionEncPrecomputedStartRspTxValid_ = false;
        memcpy(encDebug_.encLastSessionKey, connectionEncSessionKey_,
               sizeof(encDebug_.encLastSessionKey));
        encDebug_.encLastSessionKeyValid = 1U;
        encDebug_.encLastRxDir = connectionEncRxDirection_;
        encDebug_.encLastTxDir = connectionEncTxDirection_;
        emitBleTrace("ENC_KEY_DIR_SWAP");
      }
    }

    if (!decryptOk) {
      terminateInd = true;
      terminateMicFailure = true;
      ++encDebug_.encRxMicFailCount;
      encDebug_.encRxLastMicFailHdr = hdr0;
      encDebug_.encRxLastMicFailLenRaw = rxLengthRaw;
      encDebug_.encRxLastMicFailDir = connectionEncRxDirection_;
      encDebug_.encRxLastMicFailCounterLo =
          static_cast<uint32_t>(rxCounter & 0xFFFFFFFFULL);
      recordMicFailPacket(rxLengthRaw);
      emitBleTrace("ENC_RX_MIC_FAIL_POSTTX");
    } else {
      rxWasDecrypted = true;
      rxLength = plaintextLen;
      if (plaintextLen > 0U) {
        memcpy(&rxPacket_[2], plaintext, plaintextLen);
      }
      if (packetIsNew) {
        connectionEncRxCounter_ = (rxCounter + 1ULL) & kBleEncPacketCounterMask;
      }

      if ((llid == kBlePduLlControl) &&
          (rxLength >= 1U) &&
          (rxPacket_[2] == kBleLlCtrlStartEncRsp)) {
        // Keep procedure state unchanged here so the normal LL control
        // response path can queue/transmit final LL_START_ENC_RSP.
        emitBleTrace("LL_START_ENC_RSP_RX_POSTTX");
      }
    }
  }

  if (deferEncryptedDataDecrypt && !terminateInd && packetIsNew) {
    const uint64_t rxCounter = connectionEncRxCounter_;
    uint8_t plaintext[kBleDataPduMaxPayload] = {0};
    uint8_t plaintextLen = 0U;
    const bool decryptOk =
        bleCcmDecryptPayload(connectionEncSessionKey_, connectionEncIv_,
                            rxCounter, connectionEncRxDirection_, hdr0,
                            &rxPacket_[2], rxLengthRaw,
                            plaintext, &plaintextLen);

    if (!decryptOk) {
      terminateInd = true;
      terminateMicFailure = true;
      ++encDebug_.encRxMicFailCount;
      encDebug_.encRxLastMicFailHdr = hdr0;
      encDebug_.encRxLastMicFailLenRaw = rxLengthRaw;
      encDebug_.encRxLastMicFailDir = connectionEncRxDirection_;
      encDebug_.encRxLastMicFailCounterLo =
          static_cast<uint32_t>(rxCounter & 0xFFFFFFFFULL);
      recordMicFailPacket(rxLengthRaw);
      emitBleTrace("ENC_RX_MIC_FAIL_POSTTX_DATA");
    } else {
      rxWasDecrypted = true;
      rxLength = plaintextLen;
      if (plaintextLen > 0U) {
        memcpy(&rxPacket_[2], plaintext, plaintextLen);
      }
      connectionEncRxCounter_ = (rxCounter + 1ULL) & kBleEncPacketCounterMask;
    }
  }

  encDebug_.encLastRxHdr = hdr0;
  encDebug_.encLastRxLenRaw = rxLengthRaw;
  encDebug_.encLastRxWasNew = packetIsNew ? 1U : 0U;
  encDebug_.encLastRxWasDecrypted = rxWasDecrypted ? 1U : 0U;
  encDebug_.encLastRxCounterLo =
      static_cast<uint32_t>(connectionEncRxCounter_ & 0xFFFFFFFFULL);

  rememberConnectionRxDebug(
      llid, rxLength,
      bleConnectionPayloadProtocolOpcode(llid, &rxPacket_[2], rxLength), nesn, sn,
      packetIsNew, peerAckedLastTx);

  uint8_t deferredLlid = 0x01U;
  uint8_t deferredLength = 0U;
  bool deferredL2capStartedFragment = false;

  if (!terminateInd) {
    if (packetIsNew &&
        !llControlHandledImmediate &&
        llid == kBlePduLlControl &&
        rxLength >= 1U) {
      uint8_t responseLength = 0U;
      if (buildLlControlResponse(&rxPacket_[2], rxLength, currentEventCounter,
                                 connectionTxPayload_, &responseLength,
                                 &terminateInd) &&
          responseLength > 0U) {
        deferredLlid = kBlePduLlControl;
        deferredLength = responseLength;
      }
    } else if (packetIsNew &&
               !l2capHandledImmediate &&
               !encryptionProcedureBusy &&
               (((llid == kBlePduDataStartOrComplete) &&
                 (rxLength >= kBleL2capHeaderLen)) ||
                ((llid == 0x01U) && (rxLength > 0U)))) {
      uint8_t responseLength = 0U;
      const bool fragmentWasActive =
          connectionPendingL2capTxFragmentActive_;
      if (buildL2capResponse(llid, &rxPacket_[2], rxLength, packetIsNew,
                             connectionTxPayload_, &responseLength) &&
          responseLength > 0U) {
        deferredLlid = kBlePduDataStartOrComplete;
        deferredLength = responseLength;
        deferredL2capStartedFragment =
            !fragmentWasActive &&
            connectionPendingL2capTxFragmentActive_;
      }
    }

    if (deferredLength == 0U) {
      maybeQueueCentralLinkSetupRequest();
    }

    // Peripheral-initiated SMP Security Request: with no key it starts pairing;
    // with a loaded bond it asks the central to start link-layer encryption.
    if (connectionSmpSecurityRequestPending_ &&
        (smpTimedOutForConnection_ || smpBackoffActiveForCurrentPeer())) {
      connectionSmpSecurityRequestPending_ = false;
      if (!smpTimedOutForConnection_) {
        smpPairingFailureReasonPending_ = true;
        smpLastPairingFailureReason_ = kSmpReasonRepeatedAttempts;
      }
    }
    if ((deferredLength == 0U) &&
        !connectionPendingTxValid_ &&
        !encryptionProcedureBusy &&
        !connectionEncStartReqTxPending_ &&
        !connectionEncAwaitingStartRsp_ &&
        !connectionEncStartReqPending_ &&
        (smpPairingState_ == kSmpPairingStateIdle) &&
        !connectionEncSessionValid_ &&
        connectionSmpSecurityRequestPending_) {
      writeLe16(&connectionTxPayload_[0], 2U);           // L2CAP length = 2
      writeLe16(&connectionTxPayload_[2], kBleL2capCidSmp);
      connectionTxPayload_[4] = kSmpCodeSecurityRequest; // opcode 0x0B
      if (!smpSecurityPolicyLatched_) {
        latchSecurityPolicyForPairing();
      }
      connectionTxPayload_[5] = buildLocalPairingAuthReq(true);
      deferredLlid = kBlePduDataStartOrComplete;
      deferredLength = 6U;
      connectionSmpSecurityRequestPending_ = false;
      restartSmpTimer();
      emitBleTrace("SMP_SEC_REQ_TX");
    }

    if ((deferredLength == 0U) &&
        !connectionPendingTxValid_ &&
        !encryptionProcedureBusy &&
        !connectionEncStartReqTxPending_ &&
        !connectionEncAwaitingStartRsp_ &&
        !connectionEncStartReqPending_ &&
        (channelSoundingLlControlTxCount_ > 0U) &&
        dequeueChannelSoundingLlControlTx(connectionTxPayload_,
                                         &deferredLength)) {
      deferredLlid = kBlePduLlControl;
      emitBleTrace("CS_LL_TXQ");
    }

    if ((deferredLength == 0U) &&
        !connectionPendingTxValid_ &&
        !connectionPendingL2capTxFragmentActive_ &&
        !encryptionProcedureBusy &&
        !connectionEncStartReqTxPending_ &&
        !connectionEncAwaitingStartRsp_ &&
        !connectionEncStartReqPending_ &&
        takeCustomGattAuthorizationResponse(connectionTxPayload_,
                                            &deferredLength)) {
      deferredLlid = kBlePduDataStartOrComplete;
      emitBleTrace("ATT_AUTHORIZE_RSP_TXQ");
    }

    if ((deferredLength == 0U) &&
        !connectionPendingTxValid_ &&
        !encryptionProcedureBusy &&
        !connectionEncStartReqTxPending_ &&
        !connectionEncAwaitingStartRsp_ &&
        !connectionEncStartReqPending_ &&
        connectionServiceChangedIndicationsEnabled_ &&
        connectionServiceChangedCccdRestored_ &&
        connectionServiceChangedIndicationPending_ &&
        connectionEncRxEnabled_ && connectionEncTxEnabled_ &&
        bondedServiceChangedStartHandle_ != 0U &&
        !connectionServiceChangedIndicationAwaitingConfirm_ &&
        connectionCustomIndicationAwaitingHandle_ == 0U) {
      writeLe16(&connectionTxPayload_[0], 7U);
      writeLe16(&connectionTxPayload_[2], kBleL2capCidAtt);
      connectionTxPayload_[4] = kAttOpHandleValueInd;
      writeLe16(&connectionTxPayload_[5], kHandleGattServiceChangedValue);
      writeLe16(&connectionTxPayload_[7], bondedServiceChangedStartHandle_);
      writeLe16(&connectionTxPayload_[9], bondedServiceChangedEndHandle_);
      deferredLlid = kBlePduDataStartOrComplete;
      deferredLength = 11U;
      connectionServiceChangedIndicationStartHandle_ =
          bondedServiceChangedStartHandle_;
      connectionServiceChangedIndicationEndHandle_ =
          bondedServiceChangedEndHandle_;
      connectionServiceChangedIndicationGeneration_ =
          bondedServiceChangedGeneration_;
      connectionServiceChangedIndicationPending_ = false;
      connectionServiceChangedIndicationAwaitingConfirm_ = true;
    }

    if ((deferredLength == 0U) &&
        !connectionPendingTxValid_ &&
        !encryptionProcedureBusy &&
        !connectionEncStartReqTxPending_ &&
        !connectionEncAwaitingStartRsp_ &&
        !connectionEncStartReqPending_) {
      const bool securityProcedureActive =
          (smpPairingState_ != kSmpPairingStateIdle) ||
          connectionEncKeyDerivationPending_ ||
          (connectionEncSessionValid_ &&
           (!connectionEncRxEnabled_ || !connectionEncTxEnabled_));
      BleQueuedCustomNotificationState queuedCustom{};
      uint8_t queueGuard = connectionCustomNotificationQueueCount_;
      while (!securityProcedureActive &&
             (deferredLength == 0U) && (queueGuard-- > 0U) &&
             peekCustomGattNotification(&queuedCustom)) {
        if (queuedCustom.characteristicIndex >= customGattCharacteristicCount_) {
          popCustomGattNotification();
          continue;
        }

        BleCustomCharacteristicState& custom =
            customGattCharacteristics_[queuedCustom.characteristicIndex];
        const bool modeEnabled = queuedCustom.indication
                                     ? (((custom.properties & kBleGattPropIndicate) != 0U) &&
                                        ((custom.cccdValue & 0x0002U) != 0U))
                                     : (((custom.properties & kBleGattPropNotify) != 0U) &&
                                        ((custom.cccdValue & 0x0001U) != 0U));
        const bool indicationFlowReady =
            !connectionServiceChangedIndicationAwaitingConfirm_ &&
            (connectionCustomIndicationAwaitingHandle_ == 0U);
        const uint16_t requiredAttMtu =
            static_cast<uint16_t>(3U + queuedCustom.valueLength);
        const uint16_t requiredDataLength =
            static_cast<uint16_t>(7U + queuedCustom.valueLength);
        const bool payloadFitsCurrentLink =
            (currentAttMtu() >= requiredAttMtu) &&
            (currentTxDataPduPayloadLength() >= requiredDataLength);
        const bool readPermissionSatisfied =
            customGattHvxPermissionSatisfied(&custom);
        const bool canTransmit =
            connectionGattClientChangeAware_ && modeEnabled &&
            readPermissionSatisfied && payloadFitsCurrentLink &&
            (!queuedCustom.indication || indicationFlowReady);
        if (!modeEnabled) {
          popCustomGattNotification();
          continue;
        }
        if (!canTransmit) {
          break;
        }

        writeLe16(&connectionTxPayload_[0],
                  static_cast<uint16_t>(3U + queuedCustom.valueLength));
        writeLe16(&connectionTxPayload_[2], kBleL2capCidAtt);
        connectionTxPayload_[4] = queuedCustom.indication
                                      ? kAttOpHandleValueInd
                                      : kAttOpHandleValueNtf;
        writeLe16(&connectionTxPayload_[5], custom.valueHandle);
        if (queuedCustom.valueLength > 0U) {
          memcpy(&connectionTxPayload_[7], queuedCustom.value,
                 queuedCustom.valueLength);
        }
        deferredLlid = kBlePduDataStartOrComplete;
        deferredLength = static_cast<uint8_t>(7U + queuedCustom.valueLength);
        if (queuedCustom.indication) {
          connectionCustomIndicationAwaitingHandle_ = custom.valueHandle;
        }
        popCustomGattNotification();
      }
    }

    if ((deferredLength == 0U) &&
        !connectionPendingTxValid_ &&
        !encryptionProcedureBusy &&
        !connectionEncStartReqTxPending_ &&
        !connectionEncAwaitingStartRsp_ &&
        !connectionEncStartReqPending_ &&
        connectionGattClientChangeAware_ &&
        connectionBatteryNotificationsEnabled_ &&
        connectionBatteryNotificationPending_) {
      writeLe16(&connectionTxPayload_[0], 4U);
      writeLe16(&connectionTxPayload_[2], kBleL2capCidAtt);
      connectionTxPayload_[4] = kAttOpHandleValueNtf;
      writeLe16(&connectionTxPayload_[5], kHandleBatteryLevelValue);
      connectionTxPayload_[7] = gapBatteryLevel_;
      deferredLlid = kBlePduDataStartOrComplete;
      deferredLength = 8U;
      connectionBatteryNotificationPending_ = false;
    }

    if (deferredLength > 0U) {
      if (connectionPendingTxValid_ &&
          deferredLlid == kBlePduDataStartOrComplete) {
        if (deferredL2capStartedFragment) {
          (void)rewindPendingL2capTxFragment();
        } else {
          (void)deferL2capTxPayload(connectionTxPayload_, deferredLength);
        }
      } else {
        connectionPendingTxLlid_ = deferredLlid;
        connectionPendingTxLength_ = deferredLength;
        memcpy(connectionPendingTxPayload_, connectionTxPayload_, deferredLength);
        connectionPendingTxValid_ = true;
      }
    }
  }

  if (event != nullptr) {
    event->txPacketSent = txOk;
    event->txLlid = txLlid;
    event->txNesn = txNesnBit;
    event->txSn = txSnBit;
    event->txPayloadLength = txLength;
    event->txPayload =
        (txLength > 0U) ? &connectionLastTxPlainPayload_[0] : nullptr;
    event->emptyAckTransmitted = txOk && (txLlid == 0x01U) && (txLength == 0U);
    event->terminateInd = terminateInd;
  }

  if (connectionEncEnableTxOnNextEvent_ && lastTxOkForEncEnable && !terminateInd) {
    connectionEncTxEnabled_ = true;
    connectionEncEnableTxOnNextEvent_ = false;
  }

  if (terminateInd) {
    const BleDisconnectReason disconnectReason =
        terminateMicFailure
            ? BleDisconnectReason::kMicFailure
            : (peerTerminateIndReceived ? BleDisconnectReason::kPeerTerminate
                                        : BleDisconnectReason::kInternalTerminate);
    const uint8_t disconnectErrorCode =
        terminateMicFailure ? kBleLlErrorMicFailure : peerTerminateErrorCode;
    if (terminateMicFailure) {
      emitBleTrace("TERMINATE_MIC_FAILURE");
    } else if (peerTerminateIndReceived) {
      emitBleTrace("PEER_TERMINATE");
    } else {
      emitBleTrace("TERMINATE_INTERNAL");
    }
    disconnectWithReason(disconnectReason, disconnectErrorCode,
                         spinLimit / 2U + 1U);
  }

  clearRadioCoreEvents(radio_);
  return true;
}

bool BleRadio::pollConnectionEvent(BleConnectionEvent* event,
                                   uint32_t spinLimit) {
  if (!bleRunningInIsr()) {
    serviceDeferredApplicationWork();
  }

  auto dequeueQueuedEvent = [&](BleConnectionEvent* outEvent) {
    if (outEvent == nullptr) {
      return false;
    }

    resetConnectionEventStruct(outEvent);
    if (!dequeueConnectionEvent(outEvent)) {
      return false;
    }

    rememberLatestConnectionRssi(*outEvent);
    if (!bleRunningInIsr()) {
      serviceDeferredApplicationWork();
    }
    return true;
  };

  if (dequeueQueuedEvent(event)) {
    return true;
  }

  if (!bleRunningInIsr() && backgroundConnectionServiceEnabled_) {
    // Non-ISR callers share the BLE idle/yield hooks with the same radio.
    // Drive connection progress through the queued service path here as well,
    // otherwise sketch polling can race a concurrent idle-service poll and
    // advance the same connection event twice under sustained duplex traffic.
    const uint8_t queuedBefore = queuedConnectionEventCount_;
    serviceBackgroundConnection(spinLimit);
    if (dequeueQueuedEvent(event)) {
      return true;
    }
    if (event == nullptr && queuedConnectionEventCount_ != queuedBefore) {
      BleConnectionEvent discarded{};
      if (dequeueConnectionEvent(&discarded)) {
        rememberLatestConnectionRssi(discarded);
        if (!bleRunningInIsr()) {
          serviceDeferredApplicationWork();
        }
        return true;
      }
    }
    if (!bleRunningInIsr()) {
      serviceDeferredApplicationWork();
    }
    return false;
  }

  if (connectionServiceBusy_) {
    return false;
  }
  connectionServiceBusy_ = true;
  const bool ran = pollConnectionEventInternal(event, spinLimit);
  connectionServiceBusy_ = false;
  if (ran && event != nullptr) {
    rememberLatestConnectionRssi(*event);
  }
  if (!bleRunningInIsr()) {
    serviceDeferredApplicationWork();
  }
  return ran;
}
