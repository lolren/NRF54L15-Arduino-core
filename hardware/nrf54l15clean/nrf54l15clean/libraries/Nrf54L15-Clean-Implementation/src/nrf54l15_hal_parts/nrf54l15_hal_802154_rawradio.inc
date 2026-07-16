
namespace {

// Nordic's generated nRF54L errata predicates use these two FICR words for
// the die family and revision. They are distinct from the public INFO.PART and
// INFO.VARIANT fields and are read-only on every supported build target.
constexpr uintptr_t kNrf54lErrataPartAddress = 0x00FFC340UL;
constexpr uintptr_t kNrf54lErrataRevisionAddress = 0x00FFC344UL;
constexpr uint32_t kNrf54l15ErrataPart = 0x1CUL;
constexpr uint32_t kNrf54lm20aErrataPart = 0x29UL;
constexpr uint32_t kNrf54lm20bErrataPart = 0x33UL;
constexpr uintptr_t kSecureRadioBase = 0x5008A000UL;
constexpr uintptr_t kMltpan6RegisterOffset = 0x810UL;
constexpr uint32_t kMltpan6Ieee802154Value = 2UL;
constexpr int8_t kIeee802154DefaultCcaEdThresholdDbm = -75;
constexpr int8_t kNrf54lRadioEdMinimumDbm = -92;
constexpr int8_t kNrf54lRadioEdMaximumDbm = 35;
constexpr uint8_t kIeee802154AckTimerStartDppiChannel = 7U;
constexpr uint8_t kIeee802154AckTxDppiChannel = 23U;
constexpr uint8_t kIeee802154AckTimerCompareIndex = 1U;
constexpr uint8_t kIeee802154AckTimerCaptureIndex = 3U;
constexpr uint32_t kIeee802154AckTxRampStartUs = 129U;
constexpr uint32_t kIeee802154AckTimerPrescaler1Mhz = 5U;
constexpr uint16_t kIeee802154AckTxStateSettleSpins = 512U;

NRF_TIMER_Type* ieee802154AckTimer() {
  return reinterpret_cast<NRF_TIMER_Type*>(
      static_cast<uintptr_t>(nrf54l15::TIMER10_BASE));
}

NRF_DPPIC_Type* ieee802154AckDppic() {
  return reinterpret_cast<NRF_DPPIC_Type*>(
      static_cast<uintptr_t>(nrf54l15::DPPIC10_BASE));
}

void disarmIeee802154AckTx(NRF_RADIO_Type* radio) {
  NRF_TIMER_Type* const timer = ieee802154AckTimer();
  NRF_DPPIC_Type* const dppic = ieee802154AckDppic();
  dppic->CHENCLR = (1UL << kIeee802154AckTxDppiChannel);
  __DSB();
  radio->SUBSCRIBE_TXEN = 0U;
  timer->PUBLISH_COMPARE[kIeee802154AckTimerCompareIndex] = 0U;
  timer->EVENTS_COMPARE[kIeee802154AckTimerCompareIndex] = 0U;
}

void stopAndClearIeee802154AckTimer() {
  NRF_TIMER_Type* const timer = ieee802154AckTimer();
  // TIMER10 anomaly 25 requires STOP followed by CLEAR before DPPI START.
  timer->TASKS_STOP = TIMER_TASKS_STOP_TASKS_STOP_Trigger;
  timer->TASKS_CLEAR = TIMER_TASKS_CLEAR_TASKS_CLEAR_Trigger;
  __DSB();
}

bool configureIeee802154AckTimer(NRF_RADIO_Type* radio) {
  if (radio == nullptr) {
    return false;
  }
  NRF_TIMER_Type* const timer = ieee802154AckTimer();
  NRF_DPPIC_Type* const dppic = ieee802154AckDppic();
  dppic->CHENCLR = (1UL << kIeee802154AckTimerStartDppiChannel) |
                   (1UL << kIeee802154AckTxDppiChannel);
  radio->PUBLISH_DISABLED = 0U;
  radio->SUBSCRIBE_TXEN = 0U;
  timer->SUBSCRIBE_START = 0U;
  timer->PUBLISH_COMPARE[kIeee802154AckTimerCompareIndex] = 0U;
  timer->SHORTS = 0U;
  stopAndClearIeee802154AckTimer();
  timer->MODE = (TIMER_MODE_MODE_Timer << TIMER_MODE_MODE_Pos) &
                TIMER_MODE_MODE_Msk;
  timer->BITMODE =
      (TIMER_BITMODE_BITMODE_32Bit << TIMER_BITMODE_BITMODE_Pos) &
      TIMER_BITMODE_BITMODE_Msk;
  timer->PRESCALER =
      (kIeee802154AckTimerPrescaler1Mhz << TIMER_PRESCALER_PRESCALER_Pos) &
      TIMER_PRESCALER_PRESCALER_Msk;
  timer->CC[kIeee802154AckTimerCompareIndex] =
      kIeee802154AckTxRampStartUs;
  timer->SHORTS =
      (TIMER_SHORTS_COMPARE1_STOP_Enabled <<
       TIMER_SHORTS_COMPARE1_STOP_Pos) &
      TIMER_SHORTS_COMPARE1_STOP_Msk;
  timer->EVENTS_COMPARE[kIeee802154AckTimerCompareIndex] = 0U;
  timer->SUBSCRIBE_START =
      (static_cast<uint32_t>(kIeee802154AckTimerStartDppiChannel)
       << TIMER_SUBSCRIBE_START_CHIDX_Pos) |
      (TIMER_SUBSCRIBE_START_EN_Enabled << TIMER_SUBSCRIBE_START_EN_Pos);
  radio->PUBLISH_DISABLED =
      (static_cast<uint32_t>(kIeee802154AckTimerStartDppiChannel)
       << RADIO_PUBLISH_DISABLED_CHIDX_Pos) |
      (RADIO_PUBLISH_DISABLED_EN_Enabled << RADIO_PUBLISH_DISABLED_EN_Pos);
  dppic->CHENSET = (1UL << kIeee802154AckTimerStartDppiChannel);
  NVIC_DisableIRQ(TIMER10_IRQn);
  NVIC_ClearPendingIRQ(TIMER10_IRQn);
  return true;
}

void resetIeee802154AckTimerForReceive(NRF_RADIO_Type* radio) {
  disarmIeee802154AckTx(radio);
  stopAndClearIeee802154AckTimer();
  NRF_TIMER_Type* const timer = ieee802154AckTimer();
  timer->CC[kIeee802154AckTimerCompareIndex] =
      kIeee802154AckTxRampStartUs;
  timer->EVENTS_COMPARE[kIeee802154AckTimerCompareIndex] = 0U;
}

void cleanupIeee802154AckTimer(NRF_RADIO_Type* radio) {
  if (radio == nullptr) {
    return;
  }
  NRF_TIMER_Type* const timer = ieee802154AckTimer();
  NRF_DPPIC_Type* const dppic = ieee802154AckDppic();
  dppic->CHENCLR = (1UL << kIeee802154AckTimerStartDppiChannel) |
                   (1UL << kIeee802154AckTxDppiChannel);
  __DSB();
  radio->PUBLISH_DISABLED = 0U;
  radio->SUBSCRIBE_TXEN = 0U;
  timer->SUBSCRIBE_START = 0U;
  timer->PUBLISH_COMPARE[kIeee802154AckTimerCompareIndex] = 0U;
  timer->SHORTS = 0U;
  timer->EVENTS_COMPARE[kIeee802154AckTimerCompareIndex] = 0U;
  stopAndClearIeee802154AckTimer();
  NVIC_DisableIRQ(TIMER10_IRQn);
  NVIC_ClearPendingIRQ(TIMER10_IRQn);
}

bool armIeee802154AckTx(NRF_RADIO_Type* radio, const uint8_t* ackPacket) {
  if (radio == nullptr || ackPacket == nullptr) {
    return false;
  }
  NRF_TIMER_Type* const timer = ieee802154AckTimer();
  NRF_DPPIC_Type* const dppic = ieee802154AckDppic();
  dppic->CHENCLR = (1UL << kIeee802154AckTxDppiChannel);
  __DSB();
  radio->PACKETPTR = reinterpret_cast<uint32_t>(ackPacket);
  radio->EVENTS_READY = 0U;
  radio->EVENTS_TXREADY = 0U;
  radio->SHORTS =
      ((RADIO_SHORTS_TXREADY_START_Enabled << RADIO_SHORTS_TXREADY_START_Pos) &
       RADIO_SHORTS_TXREADY_START_Msk) |
      ((RADIO_SHORTS_PHYEND_DISABLE_Enabled << RADIO_SHORTS_PHYEND_DISABLE_Pos) &
       RADIO_SHORTS_PHYEND_DISABLE_Msk);
  timer->EVENTS_COMPARE[kIeee802154AckTimerCompareIndex] = 0U;
  timer->PUBLISH_COMPARE[kIeee802154AckTimerCompareIndex] =
      (static_cast<uint32_t>(kIeee802154AckTxDppiChannel)
       << TIMER_PUBLISH_COMPARE_CHIDX_Pos) |
      (TIMER_PUBLISH_COMPARE_EN_Enabled << TIMER_PUBLISH_COMPARE_EN_Pos);
  radio->SUBSCRIBE_TXEN =
      (static_cast<uint32_t>(kIeee802154AckTxDppiChannel)
       << RADIO_SUBSCRIBE_TXEN_CHIDX_Pos) |
      (RADIO_SUBSCRIBE_TXEN_EN_Enabled << RADIO_SUBSCRIBE_TXEN_EN_Pos);
  // Classify an already-expired compare before opening the DPPI channel. Once
  // the channel is enabled, never infer a miss solely from RADIO.STATE: TXEN
  // may have been accepted while the peripheral still reports Disabled.
  timer->TASKS_CAPTURE[kIeee802154AckTimerCaptureIndex] =
      TIMER_TASKS_CAPTURE_TASKS_CAPTURE_Trigger;
  const uint32_t elapsedUs = timer->CC[kIeee802154AckTimerCaptureIndex];
  if (elapsedUs >= kIeee802154AckTxRampStartUs ||
      timer->EVENTS_COMPARE[kIeee802154AckTimerCompareIndex] != 0U) {
    disarmIeee802154AckTx(radio);
    return false;
  }
  __DMB();
  dppic->CHENSET = (1UL << kIeee802154AckTxDppiChannel);
  __DSB();
  return true;
}

bool ieee802154AckTxStarted(const NRF_RADIO_Type* radio) {
  const uint32_t state =
      (radio->STATE & RADIO_STATE_STATE_Msk) >> RADIO_STATE_STATE_Pos;
  return state == RADIO_STATE_STATE_TxRu ||
         state == RADIO_STATE_STATE_TxIdle ||
         state == RADIO_STATE_STATE_Tx ||
         state == RADIO_STATE_STATE_TxDisable;
}

bool rawRadioDisabled(const NRF_RADIO_Type* radio) {
  return radio != nullptr &&
         (((radio->STATE & RADIO_STATE_STATE_Msk) >> RADIO_STATE_STATE_Pos) ==
          RADIO_STATE_STATE_Disabled);
}

uint8_t ieee802154CcaEdThresholdRegister(int8_t dbm) {
  return static_cast<uint8_t>(static_cast<int16_t>(dbm) -
                              kNrf54lRadioEdMinimumDbm);
}

bool rawRadioHfxoRunning() {
  return (((NRF_CLOCK->XO.STAT & CLOCK_XO_STAT_STATE_Msk) >>
           CLOCK_XO_STAT_STATE_Pos) == CLOCK_XO_STAT_STATE_Running);
}

void releaseOwnedRawRadioHfxo(bool* owned) {
  if (owned == nullptr || !*owned) {
    return;
  }
  ClockControl::stopHfxo();
  *owned = false;
}

bool releaseExclusiveRadioIfDisabled(Nrf54ExclusiveRadioOwner owner,
                                     uint32_t* token,
                                     NRF_RADIO_Type* radio) {
  if (token == nullptr || *token == 0U) {
    return true;
  }
  if (!nrf54l15_scrub_owned_radio_dma(owner, *token, radio)) {
    return false;
  }
  if (!nrf54l15_release_exclusive_radio(owner, *token)) {
    return false;
  }
  *token = 0U;
  return true;
}

uint32_t nrf54lErrataPart() {
  return *reinterpret_cast<const volatile uint32_t*>(
      kNrf54lErrataPartAddress);
}

uint32_t nrf54lErrataRevision() {
  return *reinterpret_cast<const volatile uint32_t*>(
      kNrf54lErrataRevisionAddress);
}

bool radioErrataIdentityMatchesBuild() {
  const uint32_t part = nrf54lErrataPart();
#if defined(NRF54L15_XXAA)
  return part == kNrf54l15ErrataPart;
#elif defined(NRF54LM20A_XXAA)
  return part == kNrf54lm20aErrataPart;
#elif defined(NRF54LM20B_XXAA)
  return part == kNrf54lm20bErrataPart;
#else
  return false;
#endif
}

bool radioAnomaly6Applies() {
  const uint32_t part = nrf54lErrataPart();
  const uint32_t revision = nrf54lErrataRevision();
  if (part == kNrf54l15ErrataPart) {
    return true;
  }
  // The current generated Nordic predicate limits MLTPAN-6 on LM20A to the
  // first engineering revision. LM20B is not listed as affected.
  return part == kNrf54lm20aErrataPart && revision == 0x00UL;
}

bool radioAnomaly20Applies() {
  const uint32_t part = nrf54lErrataPart();
  return part == kNrf54l15ErrataPart || part == kNrf54lm20aErrataPart ||
         part == kNrf54lm20bErrataPart;
}

bool applyIeee802154RadioErrata() {
  if (!radioErrataIdentityMatchesBuild()) {
    return false;
  }
  if (!radioAnomaly6Applies()) {
    return true;
  }
#if defined(NRF_TRUSTZONE_NONSECURE)
  // MLTPAN-6 requires the secure RADIO alias. A non-secure image must obtain
  // this through a secure service instead of faulting on a blind write.
  return false;
#else
  volatile uint32_t* const workaround =
      reinterpret_cast<volatile uint32_t*>(kSecureRadioBase +
                                           kMltpan6RegisterOffset);
  *workaround = kMltpan6Ieee802154Value;
  __DSB();
  return *workaround == kMltpan6Ieee802154Value;
#endif
}

}  // namespace

ZigbeeRadio::ZigbeeRadio(uint32_t radioBase)
    : radio_(reinterpret_cast<NRF_RADIO_Type*>(static_cast<uintptr_t>(radioBase))),
      radioOwnershipToken_(0U),
      initialized_(false),
      rfPathOwnedByZigbee_(false),
      hfxoOwnedByZigbee_(false),
      constantLatencyDepth_(0U),
      constantLatencyOwnedByZigbee_(false),
      receiverArmed_(false),
      energyDetectArmed_(false),
      bufferedReceiveArmed_(false),
      bufferedRxPacketIndex_(0U),
      bufferedRxQueueHead_(0U),
      bufferedRxQueueTail_(0U),
      bufferedRxQueueCount_(0U),
      bufferedRxOverflowCount_(0U),
      bufferedRxAckPrepared_(false),
      bufferedRxAckSequence_(0U),
      bufferedRxAckFramePending_(false),
      bufferedRxAckTxArmed_(false),
      bufferedRxAckQueueEntryValid_(false),
      bufferedRxAckQueueEntryIndex_(0U),
      bufferedRxAckTimerConfigured_(false),
      macAckRequestMode_(ZigbeeMacAcknowledgementRequestMode::kForceForDirectUnicast),
      macAckResponseEnabled_(false),
      channel_(15U),
      ccaThresholdDbm_(kIeee802154DefaultCcaEdThresholdDbm),
      txPacket_{0},
      rxPacket_{0},
      bufferedRxAckPacket_{0},
      bufferedRxPackets_{0},
      bufferedRxFrames_{} {}

ZigbeeRadio::~ZigbeeRadio() {
  if (initialized_ || hfxoOwnedByZigbee_ || rfPathOwnedByZigbee_ ||
      constantLatencyDepth_ != 0U || radioOwnershipToken_ != 0U) {
    end();
  }
  if (radioOwnershipToken_ != 0U) {
    (void)nrf54l15_quarantine_exclusive_radio(
        Nrf54ExclusiveRadioOwner::kZigbee802154, radioOwnershipToken_);
    nrf54l15_exclusive_radio_fail_stop();
  }
}

bool ZigbeeRadio::configureIeee802154() {
  if (radio_ == nullptr) {
    return false;
  }

  radio_->SHORTS = 0U;
  radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
  const bool disabled =
      waitRadioStateDisabledBudgeted(radio_, 0U, 200000UL);
  if (!disabled) {
    return false;
  }
  radio_->TASKS_SOFTRESET = RADIO_TASKS_SOFTRESET_TASKS_SOFTRESET_Trigger;

  radio_->MODE = ((RADIO_MODE_MODE_Ieee802154_250Kbit << RADIO_MODE_MODE_Pos) &
                  RADIO_MODE_MODE_Msk);
  // MLTPAN-6 must be applied immediately after selecting IEEE 802.15.4 mode.
  if (!applyIeee802154RadioErrata()) {
    return false;
  }
  // The CRC-qualified ACK scheduler starts TX ramp-up 129 us after the RX
  // DISABLED event. Nordic's nRF54L timing budget assumes 40 us Fast TX RU.
  radio_->TIMING = ((RADIO_TIMING_RU_Fast << RADIO_TIMING_RU_Pos) &
                    RADIO_TIMING_RU_Msk);

  uint32_t pcnf0 = 0U;
  pcnf0 |= (8UL << RADIO_PCNF0_LFLEN_Pos) & RADIO_PCNF0_LFLEN_Msk;
  pcnf0 |= (0UL << RADIO_PCNF0_S0LEN_Pos) & RADIO_PCNF0_S0LEN_Msk;
  pcnf0 |= (0UL << RADIO_PCNF0_S1LEN_Pos) & RADIO_PCNF0_S1LEN_Msk;
  pcnf0 |= (RADIO_PCNF0_S1INCL_Automatic << RADIO_PCNF0_S1INCL_Pos) &
           RADIO_PCNF0_S1INCL_Msk;
  pcnf0 |= (RADIO_PCNF0_PLEN_32bitZero << RADIO_PCNF0_PLEN_Pos) &
           RADIO_PCNF0_PLEN_Msk;
  // External Zigbee coordinators are only decoded reliably when the hardware
  // treats the PHR length as including the two-byte FCS, matching 802.15.4.
  pcnf0 |= (RADIO_PCNF0_CRCINC_Include << RADIO_PCNF0_CRCINC_Pos) &
           RADIO_PCNF0_CRCINC_Msk;
  pcnf0 |= (0UL << RADIO_PCNF0_TERMLEN_Pos) & RADIO_PCNF0_TERMLEN_Msk;
  radio_->PCNF0 = pcnf0;

  uint32_t pcnf1 = 0U;
  pcnf1 |= (127UL << RADIO_PCNF1_MAXLEN_Pos) & RADIO_PCNF1_MAXLEN_Msk;
  pcnf1 |= (0UL << RADIO_PCNF1_STATLEN_Pos) & RADIO_PCNF1_STATLEN_Msk;
  pcnf1 |= (0UL << RADIO_PCNF1_BALEN_Pos) & RADIO_PCNF1_BALEN_Msk;
  pcnf1 |= (RADIO_PCNF1_ENDIAN_Little << RADIO_PCNF1_ENDIAN_Pos) &
           RADIO_PCNF1_ENDIAN_Msk;
  pcnf1 |= (RADIO_PCNF1_WHITEEN_Disabled << RADIO_PCNF1_WHITEEN_Pos) &
           RADIO_PCNF1_WHITEEN_Msk;
  pcnf1 |= (RADIO_PCNF1_WHITEOFFSET_Include << RADIO_PCNF1_WHITEOFFSET_Pos) &
           RADIO_PCNF1_WHITEOFFSET_Msk;
  radio_->PCNF1 = pcnf1;

  uint32_t crccnf = 0U;
  crccnf |= (RADIO_CRCCNF_LEN_Two << RADIO_CRCCNF_LEN_Pos) &
            RADIO_CRCCNF_LEN_Msk;
  crccnf |= (RADIO_CRCCNF_SKIPADDR_Ieee802154 << RADIO_CRCCNF_SKIPADDR_Pos) &
            RADIO_CRCCNF_SKIPADDR_Msk;
  radio_->CRCCNF = crccnf;
  radio_->CRCPOLY = (0x11021UL & RADIO_CRCPOLY_CRCPOLY_Msk);
  radio_->CRCINIT = 0U;

  // Disable hardware address/header matching so active scan can observe any
  // nearby IEEE 802.15.4 traffic, not only frames matching a programmed table.
  for (uint8_t i = 0U; i < 8U; ++i) {
    radio_->DAB[i] = 0U;
    radio_->DAP[i] = 0U;
  }
  radio_->DACNF = 0U;
  radio_->MHRMATCHCONF = 0U;
  radio_->MHRMATCHMASK = 0U;

  // TIFS cannot be made conditional on CRCOK. TIMER10/DPPIC10 schedules the
  // 192 us immediate ACK; leave RADIO's unconditional TIFS path disabled.
  radio_->TIFS = RADIO_TIFS_ResetValue;
  radio_->SFD = 0xA7U;
  radio_->EDCTRL = RADIO_EDCTRL_ResetValue;
  radio_->CCACTRL =
      (RADIO_CCACTRL_ResetValue &
       ~(RADIO_CCACTRL_CCAMODE_Msk | RADIO_CCACTRL_CCAEDTHRES_Msk)) |
      ((RADIO_CCACTRL_CCAMODE_EdMode << RADIO_CCACTRL_CCAMODE_Pos) &
       RADIO_CCACTRL_CCAMODE_Msk) |
      ((static_cast<uint32_t>(
            ieee802154CcaEdThresholdRegister(ccaThresholdDbm_))
        << RADIO_CCACTRL_CCAEDTHRES_Pos) &
       RADIO_CCACTRL_CCAEDTHRES_Msk);

  clearRadioCoreEvents(radio_);
  radio_->EVENTS_FRAMESTART = 0U;
  radio_->EVENTS_MHRMATCH = 0U;
  radio_->EVENTS_DEVMATCH = 0U;
  radio_->EVENTS_DEVMISS = 0U;
  radio_->EVENTS_SYNC = 0U;
  radio_->EVENTS_EDEND = 0U;
  radio_->EVENTS_CCAIDLE = 0U;
  radio_->EVENTS_CCABUSY = 0U;
  radio_->EVENTS_CCASTOPPED = 0U;
  return true;
}

bool ZigbeeRadio::acquireRadioConstantLatency() {
  if (!radioAnomaly20Applies()) {
    return true;
  }

  const uint32_t primask = __get_PRIMASK();
  __disable_irq();
  if (constantLatencyDepth_ == 0U) {
    constantLatencyOwnedByZigbee_ = nrf54l15_constlat_acquire() != 0U;
    if (!constantLatencyOwnedByZigbee_) {
      __set_PRIMASK(primask);
      return false;
    }
  }
  if (constantLatencyDepth_ == UINT16_MAX) {
    __set_PRIMASK(primask);
    return false;
  }
  ++constantLatencyDepth_;
  __set_PRIMASK(primask);
  return true;
}

void ZigbeeRadio::releaseRadioConstantLatency() {
  if (!radioAnomaly20Applies()) {
    return;
  }

  const uint32_t primask = __get_PRIMASK();
  __disable_irq();
  if (constantLatencyDepth_ > 0U) {
    --constantLatencyDepth_;
  }
  if (constantLatencyDepth_ == 0U && constantLatencyOwnedByZigbee_) {
    nrf54l15_constlat_release();
    constantLatencyOwnedByZigbee_ = false;
  }
  __set_PRIMASK(primask);
}

bool ZigbeeRadio::begin(uint8_t channel, int8_t txPowerDbm) {
  if (initialized_ || radioOwnershipToken_ != 0U) {
    return false;
  }
  if (radio_ == nullptr || !ieee802154ChannelValid(channel)) {
    initialized_ = false;
    rfPathOwnedByZigbee_ = false;
    return false;
  }

  radioOwnershipToken_ = nrf54l15_acquire_exclusive_radio(
      Nrf54ExclusiveRadioOwner::kZigbee802154);
  if (radioOwnershipToken_ == 0U) {
    return false;
  }

  const bool hfxoWasRunning = rawRadioHfxoRunning();
  hfxoOwnedByZigbee_ = hfxoOwnedByZigbee_ || !hfxoWasRunning;
  if (!ClockControl::startHfxo(true, 1500000UL)) {
    initialized_ = false;
    rfPathOwnedByZigbee_ = false;
    releaseOwnedRawRadioHfxo(&hfxoOwnedByZigbee_);
    (void)releaseExclusiveRadioIfDisabled(
        Nrf54ExclusiveRadioOwner::kZigbee802154, &radioOwnershipToken_, radio_);
    return false;
  }
  const bool rfPathWasPowered = BoardControl::rfSwitchPowerEnabled();
  if (!BoardControl::enableRfPath(boardDesiredAntennaPath())) {
    initialized_ = false;
    rfPathOwnedByZigbee_ = false;
    releaseOwnedRawRadioHfxo(&hfxoOwnedByZigbee_);
    (void)releaseExclusiveRadioIfDisabled(
        Nrf54ExclusiveRadioOwner::kZigbee802154, &radioOwnershipToken_, radio_);
    return false;
  }
  rfPathOwnedByZigbee_ = !rfPathWasPowered;
  if (!configureIeee802154()) {
    initialized_ = false;
    if (rawRadioDisabled(radio_) && rfPathOwnedByZigbee_) {
      (void)BoardControl::collapseRfPathIdle();
      rfPathOwnedByZigbee_ = false;
    }
    if (rawRadioDisabled(radio_)) {
      releaseOwnedRawRadioHfxo(&hfxoOwnedByZigbee_);
      (void)releaseExclusiveRadioIfDisabled(
          Nrf54ExclusiveRadioOwner::kZigbee802154, &radioOwnershipToken_,
          radio_);
    }
    return false;
  }
  if (!setChannel(channel)) {
    initialized_ = false;
    if (rfPathOwnedByZigbee_) {
      (void)BoardControl::collapseRfPathIdle();
      rfPathOwnedByZigbee_ = false;
    }
    releaseOwnedRawRadioHfxo(&hfxoOwnedByZigbee_);
    (void)releaseExclusiveRadioIfDisabled(
        Nrf54ExclusiveRadioOwner::kZigbee802154, &radioOwnershipToken_, radio_);
    return false;
  }
  if (!setTxPowerDbm(txPowerDbm)) {
    initialized_ = false;
    if (rfPathOwnedByZigbee_) {
      (void)BoardControl::collapseRfPathIdle();
      rfPathOwnedByZigbee_ = false;
    }
    releaseOwnedRawRadioHfxo(&hfxoOwnedByZigbee_);
    (void)releaseExclusiveRadioIfDisabled(
        Nrf54ExclusiveRadioOwner::kZigbee802154, &radioOwnershipToken_, radio_);
    return false;
  }

  initialized_ = true;
  receiverArmed_ = false;
  energyDetectArmed_ = false;
  bufferedReceiveArmed_ = false;
  resetBufferedReceiveQueue();
  return true;
}

void ZigbeeRadio::end() {
  if (radioOwnershipToken_ == 0U) {
    initialized_ = false;
    receiverArmed_ = false;
    energyDetectArmed_ = false;
    bufferedReceiveArmed_ = false;
    if (g_activeZigbeeRadioIrq == this) {
      g_activeZigbeeRadioIrq = nullptr;
    }
    return;
  }
  if (!nrf54l15_exclusive_radio_is_owned_by(
          Nrf54ExclusiveRadioOwner::kZigbee802154,
          radioOwnershipToken_)) {
    return;
  }
  if (radio_ == nullptr) {
    initialized_ = false;
    rfPathOwnedByZigbee_ = false;
    releaseOwnedRawRadioHfxo(&hfxoOwnedByZigbee_);
    (void)releaseExclusiveRadioIfDisabled(
        Nrf54ExclusiveRadioOwner::kZigbee802154, &radioOwnershipToken_, radio_);
    return;
  }

  cancelBufferedReceive(200000UL);
  if (bufferedRxAckTimerConfigured_) {
    cleanupIeee802154AckTimer(radio_);
    bufferedRxAckTimerConfigured_ = false;
  }
  radio_->SHORTS = 0U;
  radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
  const bool disabled =
      waitRadioStateDisabledBudgeted(radio_, 0U, 200000UL);
  initialized_ = false;
  receiverArmed_ = false;
  energyDetectArmed_ = false;
  bufferedReceiveArmed_ = false;
  if (disabled) {
    clearRadioCoreEvents(radio_);
    radio_->EVENTS_FRAMESTART = 0U;
    radio_->EVENTS_MHRMATCH = 0U;
    radio_->EVENTS_EDEND = 0U;
    radio_->EVENTS_CCAIDLE = 0U;
    radio_->EVENTS_CCABUSY = 0U;
    radio_->EVENTS_CCASTOPPED = 0U;
    while (constantLatencyDepth_ != 0U) {
      releaseRadioConstantLatency();
    }
    if (rfPathOwnedByZigbee_) {
      (void)BoardControl::collapseRfPathIdle();
      rfPathOwnedByZigbee_ = false;
    }
    releaseOwnedRawRadioHfxo(&hfxoOwnedByZigbee_);
    (void)releaseExclusiveRadioIfDisabled(
        Nrf54ExclusiveRadioOwner::kZigbee802154, &radioOwnershipToken_, radio_);
  }
}

bool ZigbeeRadio::setChannel(uint8_t channel) {
  if (radio_ == nullptr ||
      !nrf54l15_exclusive_radio_is_owned_by(
          Nrf54ExclusiveRadioOwner::kZigbee802154,
          radioOwnershipToken_) ||
      energyDetectArmed_ ||
      !ieee802154ChannelValid(channel)) {
    return false;
  }

  if (channel_ != channel && (receiverArmed_ || bufferedReceiveArmed_)) {
    cancelReceive();
    if (!initialized_ || !rawRadioDisabled(radio_)) {
      return false;
    }
  }

  const uint8_t freq = ieee802154ChannelToFrequency(channel);
  radio_->FREQUENCY =
      ((static_cast<uint32_t>(freq) << RADIO_FREQUENCY_FREQUENCY_Pos) &
       RADIO_FREQUENCY_FREQUENCY_Msk) |
      (0UL << RADIO_FREQUENCY_MAP_Pos);

  channel_ = channel;
  return true;
}

uint8_t ZigbeeRadio::channel() const { return channel_; }

bool ZigbeeRadio::setTxPowerDbm(int8_t dbm) {
  if (radio_ == nullptr ||
      !nrf54l15_exclusive_radio_is_owned_by(
          Nrf54ExclusiveRadioOwner::kZigbee802154,
          radioOwnershipToken_)) {
    return false;
  }
  const uint32_t regValue = radioTxPowerRegFromDbm(dbm);
  radio_->TXPOWER = ((regValue << RADIO_TXPOWER_TXPOWER_Pos) &
                     RADIO_TXPOWER_TXPOWER_Msk);
  return true;
}

bool ZigbeeRadio::setCcaEnergyDetectThresholdDbm(int8_t dbm) {
  if (radio_ == nullptr ||
      !nrf54l15_exclusive_radio_is_owned_by(
          Nrf54ExclusiveRadioOwner::kZigbee802154,
          radioOwnershipToken_) ||
      dbm < kNrf54lRadioEdMinimumDbm ||
      dbm > kNrf54lRadioEdMaximumDbm) {
    return false;
  }
  // Receive may own RADIO continuously. Stage the requested value without
  // tearing down its queue; the next naturally Disabled CCA/TX boundary
  // commits it before CCASTART.
  if (!rawRadioDisabled(radio_)) {
    ccaThresholdDbm_ = dbm;
    return true;
  }
  const uint32_t ccaControl =
      (radio_->CCACTRL &
       ~(RADIO_CCACTRL_CCAMODE_Msk | RADIO_CCACTRL_CCAEDTHRES_Msk)) |
      ((RADIO_CCACTRL_CCAMODE_EdMode << RADIO_CCACTRL_CCAMODE_Pos) &
       RADIO_CCACTRL_CCAMODE_Msk) |
      ((static_cast<uint32_t>(ieee802154CcaEdThresholdRegister(dbm))
        << RADIO_CCACTRL_CCAEDTHRES_Pos) &
       RADIO_CCACTRL_CCAEDTHRES_Msk);
  radio_->CCACTRL = ccaControl;
  ccaThresholdDbm_ = dbm;
  return true;
}

int8_t ZigbeeRadio::ccaEnergyDetectThresholdDbm() const {
  return ccaThresholdDbm_;
}

bool ZigbeeRadio::transmitPreparedPacket(bool performCca, uint32_t spinLimit,
                                         uint32_t* outEndTimestampUs) {
  if (radio_ == nullptr) {
    return false;
  }
  if (spinLimit == 0U) {
    spinLimit = 1U;
  }
  if (performCca &&
      !setCcaEnergyDetectThresholdDbm(ccaThresholdDbm_)) {
    return false;
  }
  clearRadioCoreEvents(radio_);
  radio_->EVENTS_CCAIDLE = 0U;
  radio_->EVENTS_CCABUSY = 0U;
  radio_->EVENTS_CCASTOPPED = 0U;
  radio_->EVENTS_READY = 0U;
  radio_->EVENTS_FRAMESTART = 0U;

  const uint32_t txShorts =
      ((RADIO_SHORTS_TXREADY_START_Enabled << RADIO_SHORTS_TXREADY_START_Pos) &
       RADIO_SHORTS_TXREADY_START_Msk) |
      ((RADIO_SHORTS_PHYEND_DISABLE_Enabled << RADIO_SHORTS_PHYEND_DISABLE_Pos) &
       RADIO_SHORTS_PHYEND_DISABLE_Msk);
  if (!performCca) {
    radio_->SHORTS = txShorts;
    radio_->TASKS_TXEN = RADIO_TASKS_TXEN_TASKS_TXEN_Trigger;
    return waitRadioEndBudgeted(radio_, 12000U, spinLimit,
                                outEndTimestampUs);
  }

  // PACKETPTR is already the TX buffer. CCA runs from RXIDLE; on CCAIDLE the
  // hardware stops CCA and starts TX ramp-up without a software/disabled gap.
  // CCABUSY disables RADIO and never reaches TX.
  radio_->SHORTS =
      txShorts |
      ((RADIO_SHORTS_CCAIDLE_STOP_Enabled << RADIO_SHORTS_CCAIDLE_STOP_Pos) &
       RADIO_SHORTS_CCAIDLE_STOP_Msk) |
      ((RADIO_SHORTS_CCAIDLE_TXEN_Enabled << RADIO_SHORTS_CCAIDLE_TXEN_Pos) &
       RADIO_SHORTS_CCAIDLE_TXEN_Msk) |
      ((RADIO_SHORTS_CCABUSY_DISABLE_Enabled << RADIO_SHORTS_CCABUSY_DISABLE_Pos) &
       RADIO_SHORTS_CCABUSY_DISABLE_Msk);
  radio_->TASKS_RXEN = RADIO_TASKS_RXEN_TASKS_RXEN_Trigger;
  if (!waitForNonZero(&radio_->EVENTS_READY, spinLimit)) {
    return false;
  }
  radio_->TASKS_CCASTART = RADIO_TASKS_CCASTART_TASKS_CCASTART_Trigger;

  const uint32_t startUs = bleTimingUs();
  uint8_t timePollDivider = 16U;
  while (spinLimit-- > 0U) {
    if (radio_->EVENTS_PHYEND != 0U || radio_->EVENTS_END != 0U) {
      if (outEndTimestampUs != nullptr) {
        *outEndTimestampUs = bleTimingUs();
      }
      return true;
    }
    if (radio_->EVENTS_CCABUSY != 0U || radio_->EVENTS_CCASTOPPED != 0U) {
      lastTransmitDebug_.ccaBusy = true;
      return false;
    }
    if (--timePollDivider == 0U) {
      timePollDivider = 16U;
      if (static_cast<uint32_t>(bleTimingUs() - startUs) >= 12000U) {
        break;
      }
    }
  }
  return false;
}

bool ZigbeeRadio::frameRequestsMacAcknowledgement(const uint8_t* psdu,
                                                  uint8_t length,
                                                  uint8_t* outSequence) {
  if (outSequence != nullptr) {
    *outSequence = 0U;
  }
  if (psdu == nullptr || length < 3U) {
    return false;
  }

  const uint16_t frameControl = readLe16(&psdu[0]);
  const uint8_t frameType = static_cast<uint8_t>(frameControl & 0x0007U);
  const bool ackRequested = ((frameControl >> 5U) & 0x1U) != 0U;
  const bool sequenceSuppressed = ((frameControl >> 8U) & 0x1U) != 0U;
  const uint8_t destinationMode =
      static_cast<uint8_t>((frameControl >> 10U) & 0x03U);
  const uint8_t frameVersion =
      static_cast<uint8_t>((frameControl >> 12U) & 0x03U);
  if (!ackRequested || (frameType != 0x01U && frameType != 0x03U) ||
      (destinationMode != 0x02U && destinationMode != 0x03U) ||
      sequenceSuppressed || frameVersion > 0x01U) {
    return false;
  }

  if (destinationMode == 0x02U) {
    if (length < 7U) {
      return false;
    }
    if (readLe16(&psdu[5]) == 0xFFFFU) {
      return false;
    }
  }

  if (outSequence != nullptr) {
    *outSequence = psdu[2];
  }
  return true;
}

bool ZigbeeRadio::prepareMacAcknowledgementRequest(
    uint8_t* psdu, uint8_t length, uint8_t* outSequence,
    ZigbeeMacAcknowledgementRequestMode mode) {
  if (outSequence != nullptr) {
    *outSequence = 0U;
  }
  if (psdu == nullptr || length < 3U) {
    return false;
  }

  uint16_t frameControl = readLe16(&psdu[0]);
  const uint8_t frameType = static_cast<uint8_t>(frameControl & 0x0007U);
  const bool sequenceSuppressed = ((frameControl >> 8U) & 0x1U) != 0U;
  const uint8_t destinationMode =
      static_cast<uint8_t>((frameControl >> 10U) & 0x03U);
  const uint8_t frameVersion =
      static_cast<uint8_t>((frameControl >> 12U) & 0x03U);
  if ((frameType != 0x01U && frameType != 0x03U) ||
      (destinationMode != 0x02U && destinationMode != 0x03U) ||
      sequenceSuppressed || frameVersion > 0x01U) {
    return false;
  }

  if (destinationMode == 0x02U) {
    if (length < 7U) {
      return false;
    }
    if (readLe16(&psdu[5]) == 0xFFFFU) {
      return false;
    }
  }

  const bool ackRequested = ((frameControl >> 5U) & 0x1U) != 0U;
  if (mode == ZigbeeMacAcknowledgementRequestMode::kDisabled) {
    if (ackRequested) {
      frameControl &= ~(1U << 5U);
      writeLe16(&psdu[0], frameControl);
    }
    return false;
  }
  if (mode == ZigbeeMacAcknowledgementRequestMode::kRespectFrame &&
      !ackRequested) {
    return false;
  }
  if (mode == ZigbeeMacAcknowledgementRequestMode::kForceForDirectUnicast &&
      !ackRequested) {
    frameControl |= (1U << 5U);
    writeLe16(&psdu[0], frameControl);
  }
  if (outSequence != nullptr) {
    *outSequence = psdu[2];
  }
  return true;
}

bool ZigbeeRadio::waitForMacAcknowledgement(uint8_t sequence,
                                            bool* outFramePending,
                                            uint32_t spinLimit) {
  if (!initialized_ || radio_ == nullptr) {
    return false;
  }
  if (outFramePending != nullptr) {
    *outFramePending = false;
  }
  if (!acquireRadioConstantLatency()) {
    return false;
  }

  setIeee802154CrcLengthMode(radio_, true);
  clearRadioCoreEvents(radio_);
  radio_->EVENTS_FRAMESTART = 0U;
  radio_->PACKETPTR = reinterpret_cast<uint32_t>(rxPacket_);
  radio_->SHORTS =
      ((RADIO_SHORTS_RXREADY_START_Enabled << RADIO_SHORTS_RXREADY_START_Pos) &
       RADIO_SHORTS_RXREADY_START_Msk) |
      ((RADIO_SHORTS_PHYEND_DISABLE_Enabled << RADIO_SHORTS_PHYEND_DISABLE_Pos) &
       RADIO_SHORTS_PHYEND_DISABLE_Msk) |
      ((RADIO_SHORTS_ADDRESS_RSSISTART_Enabled
        << RADIO_SHORTS_ADDRESS_RSSISTART_Pos) &
       RADIO_SHORTS_ADDRESS_RSSISTART_Msk);

  radio_->TASKS_RXEN = RADIO_TASKS_RXEN_TASKS_RXEN_Trigger;
  const bool rxDone = waitRadioRxDoneBudgeted(radio_, 2500U, spinLimit);
  radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
  const bool disabled =
      waitRadioStateDisabledBudgeted(radio_, 3000U, spinLimit);
  radio_->SHORTS = 0U;
  if (!disabled || !rxDone) {
    if (disabled) {
      clearRadioCoreEvents(radio_);
      releaseRadioConstantLatency();
    } else {
      initialized_ = false;
    }
    return false;
  }

  const uint32_t crcStatus =
      (radio_->CRCSTATUS & RADIO_CRCSTATUS_CRCSTATUS_Msk) >>
      RADIO_CRCSTATUS_CRCSTATUS_Pos;
  const bool crcOkEvent = (radio_->EVENTS_CRCOK != 0U);
  const bool crcOk =
      crcOkEvent || (crcStatus == RADIO_CRCSTATUS_CRCSTATUS_CRCOk);
  if (!crcOk) {
    clearRadioCoreEvents(radio_);
    releaseRadioConstantLatency();
    return false;
  }

  const uint8_t length = ieee802154MacPayloadLengthFromPhr(rxPacket_[0]);
  ZigbeeMacAcknowledgementView acknowledgement{};
  const bool match =
      parseMacAcknowledgement(&rxPacket_[1], length, &acknowledgement) &&
      acknowledgement.valid && acknowledgement.sequence == sequence;
  if (match && outFramePending != nullptr) {
    *outFramePending = acknowledgement.framePending;
  }
  clearRadioCoreEvents(radio_);
  releaseRadioConstantLatency();
  return match;
}

bool ZigbeeRadio::sendMacAcknowledgement(uint8_t sequence, bool framePending,
                                         uint32_t spinLimit) {
  if (!initialized_ || radio_ == nullptr) {
    return false;
  }

  uint8_t ackLength = 0U;
  if (!buildMacAcknowledgement(sequence, &txPacket_[1], &ackLength,
                               framePending)) {
    return false;
  }
  txPacket_[0] = ieee802154PhrLengthFromMacPayloadLength(ackLength);
  if (txPacket_[0] == 0U) {
    return false;
  }
  if (!acquireRadioConstantLatency()) {
    return false;
  }

  clearRadioCoreEvents(radio_);
  radio_->EVENTS_FRAMESTART = 0U;
  radio_->PACKETPTR = reinterpret_cast<uint32_t>(txPacket_);
  radio_->SHORTS =
      ((RADIO_SHORTS_TXREADY_START_Enabled << RADIO_SHORTS_TXREADY_START_Pos) &
       RADIO_SHORTS_TXREADY_START_Msk) |
      ((RADIO_SHORTS_PHYEND_DISABLE_Enabled << RADIO_SHORTS_PHYEND_DISABLE_Pos) &
       RADIO_SHORTS_PHYEND_DISABLE_Msk);

  radio_->TASKS_TXEN = RADIO_TASKS_TXEN_TASKS_TXEN_Trigger;
  const bool endSeen = waitRadioEndBudgeted(radio_, 3000U, spinLimit);
  radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
  const bool disabled =
      waitRadioStateDisabledBudgeted(radio_, 3000U, spinLimit);
  radio_->SHORTS = 0U;
  if (disabled) {
    clearRadioCoreEvents(radio_);
    releaseRadioConstantLatency();
  } else {
    initialized_ = false;
  }
  return endSeen && disabled;
}

bool ZigbeeRadio::sendMacAcknowledgementAfterRxEnd(uint8_t sequence,
                                                   bool framePending,
                                                   uint32_t spinLimit) {
  if (!initialized_ || radio_ == nullptr) {
    return false;
  }

  uint8_t ackLength = 0U;
  if (!buildMacAcknowledgement(sequence, &txPacket_[1], &ackLength,
                               framePending)) {
    return false;
  }
  txPacket_[0] = ieee802154PhrLengthFromMacPayloadLength(ackLength);
  if (txPacket_[0] == 0U) {
    return false;
  }
  if (!acquireRadioConstantLatency()) {
    return false;
  }

  radio_->PACKETPTR = reinterpret_cast<uint32_t>(txPacket_);
  radio_->EVENTS_READY = 0U;
  radio_->EVENTS_TXREADY = 0U;
  radio_->EVENTS_END = 0U;
  radio_->EVENTS_PHYEND = 0U;
  radio_->EVENTS_DISABLED = 0U;
  radio_->SHORTS =
      ((RADIO_SHORTS_DISABLED_TXEN_Enabled << RADIO_SHORTS_DISABLED_TXEN_Pos) &
       RADIO_SHORTS_DISABLED_TXEN_Msk) |
      ((RADIO_SHORTS_TXREADY_START_Enabled << RADIO_SHORTS_TXREADY_START_Pos) &
       RADIO_SHORTS_TXREADY_START_Msk) |
      ((RADIO_SHORTS_PHYEND_DISABLE_Enabled << RADIO_SHORTS_PHYEND_DISABLE_Pos) &
       RADIO_SHORTS_PHYEND_DISABLE_Msk);

  radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
  const bool endSeen = waitRadioEndBudgeted(radio_, 3000U, spinLimit);
  const bool disabled =
      waitRadioStateDisabledBudgeted(radio_, 3000U, spinLimit);
  radio_->SHORTS = 0U;
  if (disabled) {
    clearRadioCoreEvents(radio_);
    releaseRadioConstantLatency();
  } else {
    initialized_ = false;
  }
  return endSeen && disabled;
}

void ZigbeeRadio::clearBufferedRxMacAcknowledgementState() {
  bufferedRxAckPrepared_ = false;
  bufferedRxAckSequence_ = 0U;
  bufferedRxAckFramePending_ = false;
  bufferedRxAckTxArmed_ = false;
  bufferedRxAckQueueEntryValid_ = false;
  bufferedRxAckQueueEntryIndex_ = 0U;
}

bool ZigbeeRadio::prepareBufferedRxMacAcknowledgementFromPrefix(
    const uint8_t* packet) {
  clearBufferedRxMacAcknowledgementState();
  if (!shouldRespondWithMacAcknowledgement() || packet == nullptr) {
    return false;
  }

  const uint8_t length = ieee802154MacPayloadLengthFromPhr(packet[0]);
  if (length < 3U || length > 127U) {
    return false;
  }

  const uint8_t* psdu = &packet[1];
  const uint16_t frameControl = readLe16(&psdu[0]);
  const uint8_t frameType = static_cast<uint8_t>(frameControl & 0x0007U);
  const bool ackRequested = ((frameControl >> 5U) & 0x1U) != 0U;
  const uint8_t destinationMode =
      static_cast<uint8_t>((frameControl >> 10U) & 0x03U);
  if (!ackRequested || (frameType != 0x01U && frameType != 0x03U) ||
      destinationMode == 0U) {
    return false;
  }

  if (!shouldAcceptReceivedMacFrame(psdu, length)) {
    return false;
  }

  const bool framePending = shouldSetMacAckFramePending(psdu, length);
  uint8_t ackLength = 0U;
  if (!buildMacAcknowledgement(psdu[2], &bufferedRxAckPacket_[1], &ackLength,
                               framePending)) {
    return false;
  }
  bufferedRxAckPacket_[0] = ieee802154PhrLengthFromMacPayloadLength(ackLength);
  if (bufferedRxAckPacket_[0] == 0U) {
    return false;
  }

  bufferedRxAckSequence_ = psdu[2];
  bufferedRxAckFramePending_ = framePending;
  bufferedRxAckPrepared_ = true;
  return true;
}

bool ZigbeeRadio::sendPreparedBufferedRxMacAcknowledgementAfterRxEnd(
    bool framePending, uint32_t spinLimit) {
  if (!initialized_ || radio_ == nullptr || !bufferedRxAckPrepared_) {
    return false;
  }
  if (framePending != bufferedRxAckFramePending_) {
    uint8_t ackLength = 0U;
    if (!buildMacAcknowledgement(bufferedRxAckSequence_,
                                 &bufferedRxAckPacket_[1], &ackLength,
                                 framePending)) {
      return false;
    }
    bufferedRxAckPacket_[0] = ieee802154PhrLengthFromMacPayloadLength(ackLength);
    if (bufferedRxAckPacket_[0] == 0U) {
      return false;
    }
    bufferedRxAckFramePending_ = framePending;
  }
  if (!acquireRadioConstantLatency()) {
    return false;
  }

  radio_->PACKETPTR = reinterpret_cast<uint32_t>(bufferedRxAckPacket_);
  radio_->EVENTS_READY = 0U;
  radio_->EVENTS_TXREADY = 0U;
  radio_->EVENTS_END = 0U;
  radio_->EVENTS_PHYEND = 0U;
  radio_->EVENTS_DISABLED = 0U;
  radio_->SHORTS =
      ((RADIO_SHORTS_DISABLED_TXEN_Enabled << RADIO_SHORTS_DISABLED_TXEN_Pos) &
       RADIO_SHORTS_DISABLED_TXEN_Msk) |
      ((RADIO_SHORTS_TXREADY_START_Enabled << RADIO_SHORTS_TXREADY_START_Pos) &
       RADIO_SHORTS_TXREADY_START_Msk) |
      ((RADIO_SHORTS_PHYEND_DISABLE_Enabled << RADIO_SHORTS_PHYEND_DISABLE_Pos) &
       RADIO_SHORTS_PHYEND_DISABLE_Msk);

  radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
  const bool endSeen = waitRadioEndBudgeted(radio_, 3000U, spinLimit);
  const bool disabled =
      waitRadioStateDisabledBudgeted(radio_, 3000U, spinLimit);
  radio_->SHORTS = 0U;
  if (disabled) {
    clearRadioCoreEvents(radio_);
    releaseRadioConstantLatency();
  } else {
    initialized_ = false;
  }
  return endSeen && disabled;
}

bool ZigbeeRadio::shouldAcceptReceivedMacFrame(const uint8_t* psdu,
                                               uint8_t length) const {
  if (macFrameReceiveFilterCallback_ == nullptr) {
    return true;
  }
  return macFrameReceiveFilterCallback_(psdu, length,
                                        macFrameReceiveFilterContext_);
}

bool ZigbeeRadio::shouldRespondWithMacAcknowledgement() const {
  return macAckResponseEnabled_ && macFrameReceiveFilterCallback_ != nullptr;
}

void ZigbeeRadio::resetBufferedReceiveQueue() {
  bufferedRxPacketIndex_ = 0U;
  bufferedRxQueueHead_ = 0U;
  bufferedRxQueueTail_ = 0U;
  bufferedRxQueueCount_ = 0U;
  bufferedRxOverflowCount_ = 0U;
  clearBufferedRxMacAcknowledgementState();
  memset(bufferedRxAckPacket_, 0, sizeof(bufferedRxAckPacket_));
  memset(bufferedRxPackets_, 0, sizeof(bufferedRxPackets_));
  memset(bufferedRxFrames_, 0, sizeof(bufferedRxFrames_));
}

bool ZigbeeRadio::pushBufferedReceiveFrameFromPacket(const uint8_t* packet,
                                                     int8_t rssiDbm) {
  if (packet == nullptr) {
    return false;
  }

  const uint8_t length = ieee802154MacPayloadLengthFromPhr(packet[0]);
  lastReceiveDebug_.lastPhr = packet[0];
  if (length == 0U || length > 127U) {
    lastReceiveDebug_.invalidLengthCount++;
    lastReceiveDebug_.lastLength = length;
    return false;
  }

  if (!shouldAcceptReceivedMacFrame(&packet[1], length)) {
    lastReceiveDebug_.filteredCount++;
    lastReceiveDebug_.lastLength = length;
    return false;
  }

  if (bufferedRxQueueCount_ >= kBufferedRxFrameQueueDepth) {
    bufferedRxOverflowCount_++;
    return false;
  }

  ZigbeeFrame& queued = bufferedRxFrames_[bufferedRxQueueTail_];
  queued.channel = channel_;
  queued.rssiDbm = rssiDbm;
  queued.length = length;
  queued.ackedWithFramePending = false;
  memcpy(queued.psdu, &packet[1], length);

  bufferedRxQueueTail_ = static_cast<uint8_t>(
      (bufferedRxQueueTail_ + 1U) % kBufferedRxFrameQueueDepth);
  bufferedRxQueueCount_++;
  lastReceiveDebug_.rxDoneCount++;
  lastReceiveDebug_.lastLength = length;
  return true;
}

bool ZigbeeRadio::beginBufferedReceive(uint32_t spinLimit) {
  if (!initialized_ || radio_ == nullptr || energyDetectArmed_) {
    return false;
  }
  if (bufferedReceiveArmed_) {
    return true;
  }
  if (receiverArmed_) {
    cancelReceive(spinLimit);
    if (!initialized_ || !rawRadioDisabled(radio_)) {
      return false;
    }
  }
  if (!acquireRadioConstantLatency()) {
    return false;
  }

  receiverArmed_ = false;
  if (!forceRadioDisabled(radio_, spinLimit)) {
    initialized_ = false;
    return false;
  }
  if (!configureIeee802154AckTimer(radio_)) {
    releaseRadioConstantLatency();
    return false;
  }
  bufferedRxAckTimerConfigured_ = true;
  resetBufferedReceiveQueue();
  setIeee802154CrcLengthMode(radio_, true);
  clearRadioCoreEvents(radio_);
  radio_->EVENTS_FRAMESTART = 0U;
  resetIeee802154AckTimerForReceive(radio_);
  radio_->PACKETPTR =
      reinterpret_cast<uint32_t>(bufferedRxPackets_[bufferedRxPacketIndex_]);
  radio_->SHORTS =
      ((RADIO_SHORTS_RXREADY_START_Enabled << RADIO_SHORTS_RXREADY_START_Pos) &
       RADIO_SHORTS_RXREADY_START_Msk) |
      ((RADIO_SHORTS_ADDRESS_RSSISTART_Enabled
        << RADIO_SHORTS_ADDRESS_RSSISTART_Pos) &
       RADIO_SHORTS_ADDRESS_RSSISTART_Msk) |
      ((RADIO_SHORTS_PHYEND_DISABLE_Enabled
        << RADIO_SHORTS_PHYEND_DISABLE_Pos) &
       RADIO_SHORTS_PHYEND_DISABLE_Msk);

  bufferedReceiveArmed_ = true;
  receiverArmed_ = true;
  g_activeZigbeeRadioIrq = this;
  radio_->INTENCLR00 = 0xFFFFFFFFUL;
  radio_->INTENSET00 = RADIO_INTENSET00_CRCOK_Msk |
                       RADIO_INTENSET00_CRCERROR_Msk |
                       RADIO_INTENSET00_DISABLED_Msk;
  NVIC_ClearPendingIRQ(RADIO_0_IRQn);
  NVIC_SetPriority(RADIO_0_IRQn, 3U);
  NVIC_EnableIRQ(RADIO_0_IRQn);
  radio_->TASKS_RXEN = RADIO_TASKS_RXEN_TASKS_RXEN_Trigger;
  return true;
}

bool ZigbeeRadio::pollBufferedReceive(ZigbeeFrame* frame) {
  if (frame == nullptr) {
    return false;
  }

  const uint32_t primask = __get_PRIMASK();
  __disable_irq();
  if (bufferedRxQueueCount_ == 0U) {
    __set_PRIMASK(primask);
    return false;
  }

  const uint8_t head = bufferedRxQueueHead_;
  *frame = bufferedRxFrames_[head];
  bufferedRxQueueHead_ =
      static_cast<uint8_t>((head + 1U) % kBufferedRxFrameQueueDepth);
  bufferedRxQueueCount_--;
  __set_PRIMASK(primask);
  return true;
}

void ZigbeeRadio::cancelBufferedReceive(uint32_t spinLimit) {
  if (radio_ == nullptr) {
    bufferedReceiveArmed_ = false;
    receiverArmed_ = false;
    if (g_activeZigbeeRadioIrq == this) {
      g_activeZigbeeRadioIrq = nullptr;
    }
    releaseRadioConstantLatency();
    bufferedRxAckTimerConfigured_ = false;
    return;
  }
  if (!bufferedReceiveArmed_ && g_activeZigbeeRadioIrq != this &&
      !bufferedRxAckTimerConfigured_) {
    return;
  }

  bufferedReceiveArmed_ = false;
  if (g_activeZigbeeRadioIrq == this) {
    g_activeZigbeeRadioIrq = nullptr;
  }
  radio_->INTENCLR00 = 0xFFFFFFFFUL;
  NVIC_ClearPendingIRQ(RADIO_0_IRQn);
  cleanupIeee802154AckTimer(radio_);
  bufferedRxAckTimerConfigured_ = false;
  clearBufferedRxMacAcknowledgementState();
  radio_->SHORTS = 0U;
  radio_->EVENTS_DISABLED = 0U;
  radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
  const bool disabled =
      waitRadioStateDisabledBudgeted(radio_, 3000U, spinLimit);
  receiverArmed_ = false;
  if (disabled) {
    clearRadioCoreEvents(radio_);
    releaseRadioConstantLatency();
  } else {
    initialized_ = false;
  }
}

bool ZigbeeRadio::bufferedReceiveArmed() const {
  return bufferedReceiveArmed_;
}

bool ZigbeeRadio::serviceBufferedReceiveIrq() {
  if (!bufferedReceiveArmed_ || radio_ == nullptr ||
      g_activeZigbeeRadioIrq != this) {
    return false;
  }
  const auto faultReceive = [this]() {
    initialized_ = false;
    bufferedReceiveArmed_ = false;
    receiverArmed_ = false;
    radio_->INTENCLR00 = 0xFFFFFFFFUL;
    if (g_activeZigbeeRadioIrq == this) {
      g_activeZigbeeRadioIrq = nullptr;
    }
  };
  const auto rearmReceive = [this, &faultReceive]() {
    if (!bufferedReceiveArmed_) {
      return true;
    }
    if (!rawRadioDisabled(radio_)) {
      faultReceive();
      return false;
    }
    const uint8_t nextPacketIndex = static_cast<uint8_t>(
        (bufferedRxPacketIndex_ + 1U) % kBufferedRxPacketCount);
    bufferedRxPacketIndex_ = nextPacketIndex;
    memset(bufferedRxPackets_[nextPacketIndex], 0,
           sizeof(bufferedRxPackets_[nextPacketIndex]));
    clearBufferedRxMacAcknowledgementState();
    resetIeee802154AckTimerForReceive(radio_);
    clearRadioCoreEvents(radio_);
    radio_->EVENTS_FRAMESTART = 0U;
    radio_->PACKETPTR =
        reinterpret_cast<uint32_t>(bufferedRxPackets_[nextPacketIndex]);
    radio_->SHORTS =
        ((RADIO_SHORTS_RXREADY_START_Enabled
          << RADIO_SHORTS_RXREADY_START_Pos) &
         RADIO_SHORTS_RXREADY_START_Msk) |
        ((RADIO_SHORTS_ADDRESS_RSSISTART_Enabled
          << RADIO_SHORTS_ADDRESS_RSSISTART_Pos) &
         RADIO_SHORTS_ADDRESS_RSSISTART_Msk) |
        ((RADIO_SHORTS_PHYEND_DISABLE_Enabled
          << RADIO_SHORTS_PHYEND_DISABLE_Pos) &
         RADIO_SHORTS_PHYEND_DISABLE_Msk);
    radio_->INTENCLR00 = 0xFFFFFFFFUL;
    radio_->INTENSET00 = RADIO_INTENSET00_CRCOK_Msk |
                         RADIO_INTENSET00_CRCERROR_Msk |
                         RADIO_INTENSET00_DISABLED_Msk;
    NVIC_ClearPendingIRQ(RADIO_0_IRQn);
    radio_->TASKS_RXEN = RADIO_TASKS_RXEN_TASKS_RXEN_Trigger;
    return true;
  };

  bool handled = false;
  if (radio_->EVENTS_CRCOK != 0U) {
    radio_->EVENTS_CRCOK = 0U;
    radio_->EVENTS_CRCERROR = 0U;
    handled = true;
    const uint8_t* const packet =
        bufferedRxPackets_[bufferedRxPacketIndex_];
    const uint8_t length = ieee802154MacPayloadLengthFromPhr(packet[0]);
    lastReceiveDebug_.lastPhr = packet[0];
    if (length == 0U || length > 127U) {
      lastReceiveDebug_.invalidLengthCount++;
      lastReceiveDebug_.lastLength = length;
    } else if (bufferedRxQueueCount_ >= kBufferedRxFrameQueueDepth) {
      bufferedRxOverflowCount_++;
    } else if (!shouldAcceptReceivedMacFrame(&packet[1], length)) {
      lastReceiveDebug_.filteredCount++;
      lastReceiveDebug_.lastLength = length;
    } else {
      uint8_t acknowledgementSequence = 0U;
      const bool acknowledgementRequested =
          shouldRespondWithMacAcknowledgement() &&
          frameRequestsMacAcknowledgement(&packet[1], length,
                                          &acknowledgementSequence);
      if (acknowledgementRequested) {
        const bool framePending =
            shouldSetMacAckFramePending(&packet[1], length);
        uint8_t ackLength = 0U;
        if (buildMacAcknowledgement(acknowledgementSequence,
                                    &bufferedRxAckPacket_[1], &ackLength,
                                    framePending)) {
          bufferedRxAckPacket_[0] =
              ieee802154PhrLengthFromMacPayloadLength(ackLength);
          bufferedRxAckSequence_ = acknowledgementSequence;
          bufferedRxAckFramePending_ = framePending;
          bufferedRxAckPrepared_ = bufferedRxAckPacket_[0] != 0U;
          if (bufferedRxAckPrepared_) {
            bufferedRxAckTxArmed_ =
                armIeee802154AckTx(radio_, bufferedRxAckPacket_);
          }
        }
      }

      const uint8_t queueIndex = bufferedRxQueueTail_;
      ZigbeeFrame& queued = bufferedRxFrames_[queueIndex];
      queued.channel = channel_;
      queued.rssiDbm = radioRssiDbm(radio_);
      queued.length = length;
      queued.ackedWithFramePending = false;
      memcpy(queued.psdu, &packet[1], length);
      lastReceiveDebug_.rxDoneCount++;
      lastReceiveDebug_.lastLength = length;
      if (bufferedRxAckTxArmed_) {
        bufferedRxAckQueueEntryValid_ = true;
        bufferedRxAckQueueEntryIndex_ = queueIndex;
      } else {
        bufferedRxQueueTail_ = static_cast<uint8_t>(
            (queueIndex + 1U) % kBufferedRxFrameQueueDepth);
        bufferedRxQueueCount_++;
      }
    }
  } else if (radio_->EVENTS_CRCERROR != 0U) {
    radio_->EVENTS_CRCERROR = 0U;
    disarmIeee802154AckTx(radio_);
    clearBufferedRxMacAcknowledgementState();
    lastReceiveDebug_.crcErrorCount++;
    lastReceiveDebug_.lastPhr =
        bufferedRxPackets_[bufferedRxPacketIndex_][0];
    handled = true;
  }

  if (radio_->EVENTS_DISABLED == 0U) {
    return handled;
  }
  radio_->EVENTS_DISABLED = 0U;
  handled = true;

  if (bufferedRxAckTxArmed_) {
    bool ackTxStarted = radio_->EVENTS_TXREADY != 0U ||
                        ieee802154AckTxStarted(radio_);
    const bool ackTxComplete =
        ackTxStarted &&
        (radio_->EVENTS_PHYEND != 0U || radio_->EVENTS_END != 0U);
    if (!ackTxComplete) {
      NRF_TIMER_Type* const ackTimer = ieee802154AckTimer();
      if (!ackTxStarted &&
          ackTimer->EVENTS_COMPARE[kIeee802154AckTimerCompareIndex] != 0U) {
        // COMPARE may have fired in the few instructions between the deadline
        // precheck and CHENSET. Give an accepted DPPI TXEN task time to become
        // visible before declaring that window missed.
        uint16_t settleSpins = kIeee802154AckTxStateSettleSpins;
        while (settleSpins-- != 0U && !ieee802154AckTxStarted(radio_)) {
          __NOP();
        }
        ackTxStarted = ieee802154AckTxStarted(radio_);
        if (!ackTxStarted) {
          disarmIeee802154AckTx(radio_);
          stopAndClearIeee802154AckTimer();
          bufferedRxAckTxArmed_ = false;
          if (bufferedRxAckQueueEntryValid_) {
            bufferedRxQueueTail_ = static_cast<uint8_t>(
                (bufferedRxAckQueueEntryIndex_ + 1U) %
                kBufferedRxFrameQueueDepth);
            bufferedRxQueueCount_++;
          }
          clearBufferedRxMacAcknowledgementState();
          return rearmReceive() && handled;
        }
      }
      // This is the RX DISABLED event that started TIMER10. Remove RX end
      // evidence so only the subsequent ACK TX completion can commit the slot.
      radio_->EVENTS_END = 0U;
      radio_->EVENTS_PHYEND = 0U;
      return true;
    }
    disarmIeee802154AckTx(radio_);
    stopAndClearIeee802154AckTimer();
    if (bufferedRxAckQueueEntryValid_) {
      ZigbeeFrame& queued =
          bufferedRxFrames_[bufferedRxAckQueueEntryIndex_];
      queued.ackedWithFramePending = bufferedRxAckFramePending_;
      bufferedRxQueueTail_ = static_cast<uint8_t>(
          (bufferedRxAckQueueEntryIndex_ + 1U) % kBufferedRxFrameQueueDepth);
      bufferedRxQueueCount_++;
    }
  } else {
    stopAndClearIeee802154AckTimer();
  }

  return rearmReceive() && handled;
}

bool ZigbeeRadio::beginReceive(uint32_t spinLimit) {
  return beginBufferedReceive(spinLimit);
}

bool ZigbeeRadio::pollReceive(ZigbeeFrame* frame, uint32_t spinLimit) {
  (void)spinLimit;
  return initialized_ && receiverArmed_ && bufferedReceiveArmed_ &&
         pollBufferedReceive(frame);
}

void ZigbeeRadio::cancelReceive(uint32_t spinLimit) {
  if (radio_ == nullptr) {
    receiverArmed_ = false;
    bufferedReceiveArmed_ = false;
    releaseRadioConstantLatency();
    return;
  }
  if (bufferedReceiveArmed_) {
    cancelBufferedReceive(spinLimit);
    return;
  }
  if (receiverArmed_) {
    radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
    const bool disabled =
        waitRadioStateDisabledBudgeted(radio_, 3000U, spinLimit);
    radio_->SHORTS = 0U;
    if (disabled) {
      clearRadioCoreEvents(radio_);
      releaseRadioConstantLatency();
    } else {
      initialized_ = false;
    }
  }
  receiverArmed_ = false;
}

bool ZigbeeRadio::receiverArmed() const { return receiverArmed_; }

bool ZigbeeRadio::transmit(const uint8_t* psdu, uint8_t length, bool performCca,
                           uint32_t spinLimit) {
  if (!initialized_ || radio_ == nullptr || psdu == nullptr ||
      energyDetectArmed_) {
    return false;
  }
  if (length == 0U || length > 125U) {
    return false;
  }

  if (bufferedReceiveArmed_) {
    cancelBufferedReceive(spinLimit);
  } else if (receiverArmed_) {
    cancelReceive(spinLimit);
  }
  if (!initialized_ || !rawRadioDisabled(radio_)) {
    return false;
  }
  lastTransmitDebug_ = {};
  lastTransmitDebug_.txLength = length;
  if (!acquireRadioConstantLatency()) {
    return false;
  }

  receiverArmed_ = false;
  if (!forceRadioDisabled(radio_, spinLimit)) {
    initialized_ = false;
    return false;
  }

  txPacket_[0] = ieee802154PhrLengthFromMacPayloadLength(length);
  memcpy(&txPacket_[1], psdu, length);
  uint8_t acknowledgementSequence = 0U;
  const bool waitForAck = prepareMacAcknowledgementRequest(
      &txPacket_[1], length, &acknowledgementSequence, macAckRequestMode_);
  lastTransmitDebug_.ackRequested = waitForAck;
  lastTransmitDebug_.ackSequence = acknowledgementSequence;

  radio_->PACKETPTR = reinterpret_cast<uint32_t>(txPacket_);
  const bool endSeen = transmitPreparedPacket(performCca, spinLimit);
  lastTransmitDebug_.endSeen = endSeen;

  radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
  const bool disabled =
      waitRadioStateDisabledBudgeted(radio_, 3000U, spinLimit);
  lastTransmitDebug_.disabledSeen = disabled;
  radio_->SHORTS = 0U;
  const bool acknowledged =
      (!waitForAck || !(endSeen && disabled))
          ? (endSeen && disabled)
          : waitForMacAcknowledgement(acknowledgementSequence,
                                      &lastTransmitDebug_.ackFramePending,
                                      spinLimit);
  lastTransmitDebug_.ackReceived = acknowledged;
  if (waitForAck && endSeen && disabled) {
    lastTransmitDebug_.rxLength = ieee802154MacPayloadLengthFromPhr(rxPacket_[0]);
    if (lastTransmitDebug_.rxLength >= 3U) {
      lastTransmitDebug_.rxSequence = rxPacket_[3];
    }
  }
  if (disabled) {
    clearRadioCoreEvents(radio_);
    releaseRadioConstantLatency();
  } else {
    initialized_ = false;
  }
  return endSeen && disabled && acknowledged;
}

bool ZigbeeRadio::transmitThenReceive(const uint8_t* psdu, uint8_t length,
                                      ZigbeeFrame* frame,
                                      uint32_t listenWindowUs,
                                      bool performCca, uint32_t spinLimit) {
  if (!initialized_ || radio_ == nullptr || psdu == nullptr || frame == nullptr) {
    return false;
  }
  if (energyDetectArmed_) {
    return false;
  }
  if (length == 0U || length > 125U) {
    return false;
  }

  if (bufferedReceiveArmed_) {
    cancelBufferedReceive(spinLimit);
  } else if (receiverArmed_) {
    cancelReceive(spinLimit);
  }
  if (!initialized_ || !rawRadioDisabled(radio_)) {
    return false;
  }
  lastTransmitDebug_ = {};
  lastTransmitDebug_.txLength = length;
  if (!acquireRadioConstantLatency()) {
    return false;
  }

  receiverArmed_ = false;
  if (!forceRadioDisabled(radio_, spinLimit)) {
    initialized_ = false;
    return false;
  }

  txPacket_[0] = ieee802154PhrLengthFromMacPayloadLength(length);
  memcpy(&txPacket_[1], psdu, length);
  memset(rxPacket_, 0, sizeof(rxPacket_));
  uint8_t acknowledgementSequence = 0U;
  bool awaitingMacAcknowledgement = prepareMacAcknowledgementRequest(
      &txPacket_[1], length, &acknowledgementSequence, macAckRequestMode_);
  lastTransmitDebug_.ackRequested = awaitingMacAcknowledgement;
  lastTransmitDebug_.ackSequence = acknowledgementSequence;

  radio_->PACKETPTR = reinterpret_cast<uint32_t>(txPacket_);
  uint32_t txEndTimestampUs = 0U;
  const bool txEndSeen = transmitPreparedPacket(
      performCca, spinLimit, &txEndTimestampUs);
  lastTransmitDebug_.endSeen = txEndSeen;
  lastTransmitDebug_.txEndTimestampUs = txEndTimestampUs;
  if (!txEndSeen) {
    radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
    lastTransmitDebug_.disabledSeen =
        waitRadioStateDisabledBudgeted(radio_, 3000U, spinLimit);
    radio_->SHORTS = 0U;
    if (lastTransmitDebug_.disabledSeen) {
      clearRadioCoreEvents(radio_);
      releaseRadioConstantLatency();
    } else {
      initialized_ = false;
    }
    return false;
  }

  radio_->EVENTS_READY = 0U;
  radio_->EVENTS_RXREADY = 0U;
  radio_->EVENTS_FRAMESTART = 0U;
  radio_->EVENTS_ADDRESS = 0U;
  radio_->EVENTS_PAYLOAD = 0U;
  radio_->EVENTS_CRCOK = 0U;
  radio_->EVENTS_CRCERROR = 0U;
  radio_->EVENTS_RXADDRESS = 0U;
  radio_->EVENTS_DEVMATCH = 0U;
  radio_->EVENTS_DEVMISS = 0U;
  radio_->EVENTS_MHRMATCH = 0U;
  radio_->EVENTS_SYNC = 0U;
  radio_->EVENTS_DISABLED = 0U;
  setIeee802154CrcLengthMode(radio_, true);
  radio_->PACKETPTR = reinterpret_cast<uint32_t>(rxPacket_);
  radio_->SHORTS =
      ((RADIO_SHORTS_RXREADY_START_Enabled
        << RADIO_SHORTS_RXREADY_START_Pos) &
       RADIO_SHORTS_RXREADY_START_Msk) |
      ((RADIO_SHORTS_ADDRESS_RSSISTART_Enabled
        << RADIO_SHORTS_ADDRESS_RSSISTART_Pos) &
       RADIO_SHORTS_ADDRESS_RSSISTART_Msk) |
      ((RADIO_SHORTS_PHYEND_DISABLE_Enabled
        << RADIO_SHORTS_PHYEND_DISABLE_Pos) &
       RADIO_SHORTS_PHYEND_DISABLE_Msk);

  bool txDisabled =
      waitRadioStateDisabledBudgeted(radio_, 3000U, spinLimit);
  if (!txDisabled) {
    radio_->SHORTS = 0U;
    initialized_ = false;
    return false;
  }
  lastTransmitDebug_.disabledSeen = txDisabled;
  lastTransmitDebug_.disabledTimestampUs = bleTimingUs();
  // PHYEND_DISABLE has already completed before PACKETPTR can be switched to
  // the RX buffer. A DISABLED_RXEN shortcut installed here would miss that
  // event, so start the prepared receiver explicitly from verified DISABLED.
  radio_->TASKS_RXEN = RADIO_TASKS_RXEN_TASKS_RXEN_Trigger;

  const uint32_t receiveWindowStartUs = micros();
  bool rxPrimed = true;
  while (static_cast<uint32_t>(micros() - receiveWindowStartUs) <
         listenWindowUs) {
    const uint32_t elapsedUs =
        static_cast<uint32_t>(micros() - receiveWindowStartUs);
    const uint32_t remainingUs =
        (elapsedUs < listenWindowUs) ? (listenWindowUs - elapsedUs) : 0U;
    if (remainingUs == 0U) {
      break;
    }

    if (!rxPrimed) {
      memset(rxPacket_, 0, sizeof(rxPacket_));
      clearRadioCoreEvents(radio_);
      radio_->EVENTS_FRAMESTART = 0U;
      radio_->PACKETPTR = reinterpret_cast<uint32_t>(rxPacket_);
      radio_->SHORTS =
          ((RADIO_SHORTS_RXREADY_START_Enabled
            << RADIO_SHORTS_RXREADY_START_Pos) &
           RADIO_SHORTS_RXREADY_START_Msk) |
          ((RADIO_SHORTS_ADDRESS_RSSISTART_Enabled
            << RADIO_SHORTS_ADDRESS_RSSISTART_Pos) &
           RADIO_SHORTS_ADDRESS_RSSISTART_Msk) |
          ((RADIO_SHORTS_PHYEND_DISABLE_Enabled
            << RADIO_SHORTS_PHYEND_DISABLE_Pos) &
           RADIO_SHORTS_PHYEND_DISABLE_Msk);

      radio_->TASKS_RXEN = RADIO_TASKS_RXEN_TASKS_RXEN_Trigger;
      lastTransmitDebug_.rxAttempted = true;
    } else {
      lastTransmitDebug_.rxAttempted = true;
      rxPrimed = false;
    }
    uint32_t rxDoneTimestampUs = 0U;
    const bool rxDone =
        waitRadioRxDoneBudgeted(radio_, remainingUs, spinLimit,
                                &rxDoneTimestampUs);
    lastTransmitDebug_.rxDone = rxDone;
    lastTransmitDebug_.rxDoneTimestampUs = rxDoneTimestampUs;
    const int8_t rssiDbm = radioRssiDbm(radio_);

    const uint32_t crcStatus =
        (radio_->CRCSTATUS & RADIO_CRCSTATUS_CRCSTATUS_Msk) >>
        RADIO_CRCSTATUS_CRCSTATUS_Pos;
    const bool crcOkEvent = (radio_->EVENTS_CRCOK != 0U);
    const bool crcOk =
        rxDone &&
        (crcOkEvent || (crcStatus == RADIO_CRCSTATUS_CRCSTATUS_CRCOk));
    lastTransmitDebug_.crcOk = crcOk;
    lastTransmitDebug_.crcError = rxDone && !crcOk;

    uint8_t rxLength = 0U;
    bool validLength = false;
    if (crcOk) {
      rxLength = ieee802154MacPayloadLengthFromPhr(rxPacket_[0]);
      validLength = (rxLength != 0U && rxLength <= 127U);
    }

    ZigbeeMacAcknowledgementView macAcknowledgement{};
    const bool matchedMacAcknowledgement =
        awaitingMacAcknowledgement && validLength &&
        parseMacAcknowledgement(&rxPacket_[1], rxLength, &macAcknowledgement) &&
        macAcknowledgement.valid &&
        macAcknowledgement.sequence == acknowledgementSequence;
    if (matchedMacAcknowledgement && macAcknowledgement.framePending) {
      awaitingMacAcknowledgement = false;
      lastTransmitDebug_.ackReceived = true;
      lastTransmitDebug_.ackFramePending = true;
      lastTransmitDebug_.rxLength = rxLength;
      if (rxLength >= 3U) {
        lastTransmitDebug_.rxSequence = rxPacket_[3];
      }
      // Preserve the hardware DISABLED->RXEN turnaround after a frame-pending
      // MAC ACK. Clearing DISABLED/READY/RXREADY here can race the start of
      // the pending indirect data frame and make sleepy-child Trust Center
      // traffic disappear on the joiner side.
      radio_->EVENTS_FRAMESTART = 0U;
      radio_->EVENTS_ADDRESS = 0U;
      radio_->EVENTS_PAYLOAD = 0U;
      radio_->EVENTS_CRCOK = 0U;
      radio_->EVENTS_CRCERROR = 0U;
      radio_->EVENTS_RXADDRESS = 0U;
      radio_->EVENTS_DEVMATCH = 0U;
      radio_->EVENTS_DEVMISS = 0U;
      radio_->EVENTS_MHRMATCH = 0U;
      radio_->EVENTS_SYNC = 0U;
      const uint32_t radioState =
          (radio_->STATE & RADIO_STATE_STATE_Msk) >> RADIO_STATE_STATE_Pos;
      if (radioState == RADIO_STATE_STATE_Disabled) {
        radio_->TASKS_RXEN = RADIO_TASKS_RXEN_TASKS_RXEN_Trigger;
      }

      rxPrimed = true;
      continue;
    }

    radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
    bool disabled =
        waitRadioStateDisabledBudgeted(radio_, 3000U, spinLimit);
    if (!disabled) {
      radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
      disabled =
          waitRadioStateDisabledBudgeted(radio_, 3000U, spinLimit);
    }
    snapshotZigbeeReceiveEvents(radio_, &lastTransmitDebug_);
    radio_->SHORTS = 0U;
    if (!disabled || !rxDone) {
      if (!disabled) {
        initialized_ = false;
        return false;
      }
      continue;
    }
    if (!crcOk) {
      continue;
    }
    if (!validLength) {
      continue;
    }

    if (awaitingMacAcknowledgement &&
        parseMacAcknowledgement(&rxPacket_[1], rxLength, &macAcknowledgement) &&
        macAcknowledgement.valid &&
        macAcknowledgement.sequence == acknowledgementSequence) {
      awaitingMacAcknowledgement = false;
      lastTransmitDebug_.ackReceived = true;
      lastTransmitDebug_.ackFramePending = macAcknowledgement.framePending;
      lastTransmitDebug_.rxLength = rxLength;
      if (rxLength >= 3U) {
        lastTransmitDebug_.rxSequence = rxPacket_[3];
      }
      continue;
    }

    if (!shouldAcceptReceivedMacFrame(&rxPacket_[1], rxLength)) {
      clearRadioCoreEvents(radio_);
      continue;
    }

    uint8_t responseAckSequence = 0U;
    const bool sendAcknowledgement = frameRequestsMacAcknowledgement(
        &rxPacket_[1], rxLength, &responseAckSequence);

    frame->channel = channel_;
    frame->rssiDbm = rssiDbm;
    frame->length = rxLength;
    frame->ackedWithFramePending = false;
    memcpy(frame->psdu, &rxPacket_[1], rxLength);
    lastTransmitDebug_.ackReceived =
        (!lastTransmitDebug_.ackRequested || !awaitingMacAcknowledgement);
    lastTransmitDebug_.rxLength = rxLength;
    if (rxLength >= 3U) {
      lastTransmitDebug_.rxSequence = rxPacket_[3];
    }
    if (sendAcknowledgement && shouldRespondWithMacAcknowledgement()) {
      const bool framePending =
          shouldSetMacAckFramePending(&rxPacket_[1], rxLength);
      frame->ackedWithFramePending = framePending;
      (void)sendMacAcknowledgement(responseAckSequence, framePending, spinLimit);
    }
    clearRadioCoreEvents(radio_);
    releaseRadioConstantLatency();
    return true;
  }

  lastTransmitDebug_.ackReceived =
      (!lastTransmitDebug_.ackRequested || !awaitingMacAcknowledgement);
  radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
  const bool disabled =
      waitRadioStateDisabledBudgeted(radio_, 3000U, spinLimit);
  radio_->SHORTS = 0U;
  if (disabled) {
    clearRadioCoreEvents(radio_);
    releaseRadioConstantLatency();
  } else {
    initialized_ = false;
  }
  return false;
}

bool ZigbeeRadio::receive(ZigbeeFrame* frame, uint32_t listenWindowUs,
                          uint32_t spinLimit) {
  if (!initialized_ || radio_ == nullptr || frame == nullptr) {
    return false;
  }

  const uint32_t receiveWindowStartUs = micros();
  if (!beginReceive(spinLimit)) {
    return false;
  }
  while (static_cast<uint32_t>(micros() - receiveWindowStartUs) <
         listenWindowUs) {
    if (pollReceive(frame, spinLimit)) {
      return true;
    }
    delayMicroseconds(100);
  }

  cancelReceive(spinLimit);
  return false;
}

bool ZigbeeRadio::sampleEnergyDetect(uint8_t* outEdLevel, uint32_t spinLimit) {
  if (!initialized_ || radio_ == nullptr || outEdLevel == nullptr ||
      energyDetectArmed_) {
    return false;
  }

  if (bufferedReceiveArmed_) {
    cancelBufferedReceive(spinLimit);
  } else if (receiverArmed_) {
    cancelReceive(spinLimit);
  }
  if (!initialized_ || !rawRadioDisabled(radio_)) {
    return false;
  }
  if (!acquireRadioConstantLatency()) {
    return false;
  }
  receiverArmed_ = false;
  clearRadioCoreEvents(radio_);
  radio_->EVENTS_EDEND = 0U;
  radio_->EVENTS_EDSTOPPED = 0U;
  radio_->EDCTRL = RADIO_EDCTRL_ResetValue;
  // Product specification 8.17.12.3: READY->START must be disabled so the
  // baseband does not decode incoming frames during an ED measurement.
  // READY->EDSTART begins the single 8-symbol measurement only after RX is
  // ready; EDEND->DISABLE then returns the peripheral to a known state.
  radio_->SHORTS =
      ((RADIO_SHORTS_READY_EDSTART_Enabled
        << RADIO_SHORTS_READY_EDSTART_Pos) &
       RADIO_SHORTS_READY_EDSTART_Msk) |
      ((RADIO_SHORTS_EDEND_DISABLE_Enabled
        << RADIO_SHORTS_EDEND_DISABLE_Pos) &
       RADIO_SHORTS_EDEND_DISABLE_Msk);
  radio_->TASKS_RXEN = RADIO_TASKS_RXEN_TASKS_RXEN_Trigger;
  if (!waitForNonZero(&radio_->EVENTS_EDEND, spinLimit)) {
    radio_->TASKS_EDSTOP = RADIO_TASKS_EDSTOP_TASKS_EDSTOP_Trigger;
    radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
    const bool disabled =
        waitRadioStateDisabledBudgeted(radio_, 0U, spinLimit);
    radio_->SHORTS = 0U;
    if (disabled) {
      releaseRadioConstantLatency();
    } else {
      initialized_ = false;
    }
    return false;
  }

  const uint8_t edLevel =
      static_cast<uint8_t>((radio_->EDSAMPLE & RADIO_EDSAMPLE_EDLVL_Msk) >>
                           RADIO_EDSAMPLE_EDLVL_Pos);
  *outEdLevel = edLevel;

  bool disabled = waitRadioStateDisabledBudgeted(radio_, 0U, spinLimit);
  if (!disabled) {
    radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
    disabled = waitRadioStateDisabledBudgeted(radio_, 0U, spinLimit);
  }
  radio_->SHORTS = 0U;
  if (disabled) {
    releaseRadioConstantLatency();
  } else {
    initialized_ = false;
  }
  return disabled;
}

bool ZigbeeRadio::beginEnergyDetectScan(uint16_t durationMs,
                                        uint32_t spinLimit) {
  if (!initialized_ || radio_ == nullptr || energyDetectArmed_) {
    return false;
  }
  if (bufferedReceiveArmed_) {
    cancelBufferedReceive(spinLimit);
  } else if (receiverArmed_) {
    cancelReceive(spinLimit);
  }
  if (!initialized_ || !rawRadioDisabled(radio_)) {
    return false;
  }

  // ED period is fixed at 128 us. EDCNT encodes iterations minus one and the
  // peripheral retains the maximum EDSAMPLE over all iterations.
  const uint32_t requestedUs = static_cast<uint32_t>(durationMs) * 1000UL;
  uint32_t iterations =
      (requestedUs == 0U) ? 1U : ((requestedUs + 127U) / 128U);
  const uint32_t maxIterations =
      (RADIO_EDCTRL_EDCNT_Msk >> RADIO_EDCTRL_EDCNT_Pos) + 1UL;
  if (iterations > maxIterations) {
    iterations = maxIterations;
  }

  if (!acquireRadioConstantLatency()) {
    return false;
  }
  clearRadioCoreEvents(radio_);
  radio_->EVENTS_EDEND = 0U;
  radio_->EVENTS_EDSTOPPED = 0U;
  radio_->EDCTRL =
      (RADIO_EDCTRL_ResetValue & ~RADIO_EDCTRL_EDCNT_Msk) |
      (((iterations - 1U) << RADIO_EDCTRL_EDCNT_Pos) &
       RADIO_EDCTRL_EDCNT_Msk);
  radio_->SHORTS =
      ((RADIO_SHORTS_READY_EDSTART_Enabled
        << RADIO_SHORTS_READY_EDSTART_Pos) &
       RADIO_SHORTS_READY_EDSTART_Msk) |
      ((RADIO_SHORTS_EDEND_DISABLE_Enabled
        << RADIO_SHORTS_EDEND_DISABLE_Pos) &
       RADIO_SHORTS_EDEND_DISABLE_Msk);
  energyDetectArmed_ = true;
  radio_->TASKS_RXEN = RADIO_TASKS_RXEN_TASKS_RXEN_Trigger;
  return true;
}

bool ZigbeeRadio::pollEnergyDetectScan(uint8_t* outEdLevel, bool* outComplete,
                                       uint32_t spinLimit) {
  if (outComplete != nullptr) {
    *outComplete = false;
  }
  if (!initialized_ || radio_ == nullptr || outEdLevel == nullptr ||
      outComplete == nullptr || !energyDetectArmed_) {
    return false;
  }
  if (radio_->EVENTS_EDEND == 0U) {
    return true;
  }

  const uint8_t edLevel =
      static_cast<uint8_t>((radio_->EDSAMPLE & RADIO_EDSAMPLE_EDLVL_Msk) >>
                           RADIO_EDSAMPLE_EDLVL_Pos);
  bool disabled = waitRadioStateDisabledBudgeted(radio_, 0U, spinLimit);
  if (!disabled) {
    radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
    disabled = waitRadioStateDisabledBudgeted(radio_, 0U, spinLimit);
  }
  radio_->SHORTS = 0U;
  if (!disabled) {
    initialized_ = false;
    return false;
  }

  radio_->EDCTRL = RADIO_EDCTRL_ResetValue;
  clearRadioCoreEvents(radio_);
  energyDetectArmed_ = false;
  releaseRadioConstantLatency();
  *outEdLevel = edLevel;
  *outComplete = true;
  return true;
}

ZigbeeTransmitDebug ZigbeeRadio::lastTransmitDebug() const {
  return lastTransmitDebug_;
}

ZigbeeReceiveDebug ZigbeeRadio::lastReceiveDebug() const {
  return lastReceiveDebug_;
}

void ZigbeeRadio::setMacAcknowledgementRequestMode(
    ZigbeeMacAcknowledgementRequestMode mode) {
  macAckRequestMode_ = mode;
}

ZigbeeMacAcknowledgementRequestMode
ZigbeeRadio::macAcknowledgementRequestMode() const {
  return macAckRequestMode_;
}

void ZigbeeRadio::setMacAcknowledgementResponseEnabled(bool enabled) {
  macAckResponseEnabled_ = enabled;
}

bool ZigbeeRadio::macAcknowledgementResponseEnabled() const {
  return macAckResponseEnabled_;
}

void ZigbeeRadio::setMacDataRequestPendingCallback(
    MacDataRequestPendingCallback callback, void* context) {
  const uint32_t primask = __get_PRIMASK();
  __disable_irq();
  macDataRequestPendingCallback_ = callback;
  macDataRequestPendingContext_ = context;
  __set_PRIMASK(primask);
}

void ZigbeeRadio::setMacFrameReceiveFilterCallback(
    MacFrameReceiveFilterCallback callback, void* context) {
  const uint32_t primask = __get_PRIMASK();
  __disable_irq();
  macFrameReceiveFilterCallback_ = callback;
  macFrameReceiveFilterContext_ = context;
  __set_PRIMASK(primask);
}

namespace {
constexpr uint16_t kMacFcfSecurityEnabled = 1U << 3U;
constexpr uint16_t kMacFcfSequenceSuppression = 1U << 8U;
constexpr uint8_t kMacSecurityLevelMask = 0x07U;
constexpr uint8_t kMacKeyIdModeMask = 0x18U;
constexpr uint8_t kMacKeyIdMode0 = 0U << 3U;
constexpr uint8_t kMacKeyIdMode1 = 1U << 3U;
constexpr uint8_t kMacKeyIdMode2 = 2U << 3U;
constexpr uint8_t kMacKeyIdMode3 = 3U << 3U;

bool skipMacAuxSecurityHeader(const uint8_t* psdu, uint8_t length,
                              size_t* index) {
  if (psdu == nullptr || index == nullptr || *index >= length) {
    return false;
  }

  const uint8_t securityControl = psdu[*index];
  if ((securityControl & kMacSecurityLevelMask) == 0U) {
    return false;
  }

  size_t headerSize = 1U + 4U;
  switch (securityControl & kMacKeyIdModeMask) {
    case kMacKeyIdMode0:
      break;
    case kMacKeyIdMode1:
      headerSize += 1U;
      break;
    case kMacKeyIdMode2:
      headerSize += 4U + 1U;
      break;
    case kMacKeyIdMode3:
      headerSize += 8U + 1U;
      break;
    default:
      return false;
  }

  if ((length - *index) < headerSize) {
    return false;
  }

  *index += headerSize;
  return true;
}
}  // namespace

bool ZigbeeRadio::shouldSetMacAckFramePending(const uint8_t* psdu,
                                              uint8_t length) const {
  if (psdu == nullptr || length < 4U) {
    return false;
  }

  const uint16_t frameControl = readLe16(&psdu[0]);
  const uint8_t frameType = static_cast<uint8_t>(frameControl & 0x0007U);
  if (frameType != 0x03U) {
    return false;
  }

  if (macDataRequestPendingCallback_ == nullptr) {
    return false;
  }

  const bool panCompression = ((frameControl >> 6U) & 0x1U) != 0U;
  const bool securityEnabled = (frameControl & kMacFcfSecurityEnabled) != 0U;
  const bool sequenceSuppressed =
      (frameControl & kMacFcfSequenceSuppression) != 0U;
  const uint8_t destinationMode =
      static_cast<uint8_t>((frameControl >> 10U) & 0x03U);
  const uint8_t sourceMode =
      static_cast<uint8_t>((frameControl >> 14U) & 0x03U);

  size_t index = sequenceSuppressed ? 2U : 3U;
  if (destinationMode != 0U) {
    if (length < (index + 2U)) {
      return false;
    }
    index += 2U;
    if (destinationMode == 0x02U) {
      index += 2U;
    } else if (destinationMode == 0x03U) {
      index += 8U;
    } else {
      return false;
    }
  }

  if (sourceMode != 0U && (!panCompression || destinationMode == 0U)) {
    index += 2U;
  }

  if (sourceMode == 0x02U) {
    if (length < (index + 2U)) {
      return false;
    }
    index += 2U;
  } else if (sourceMode == 0x03U) {
    if (length < (index + 8U)) {
      return false;
    }
    index += 8U;
  } else {
    return false;
  }

  if (securityEnabled && !skipMacAuxSecurityHeader(psdu, length, &index)) {
    return false;
  }
  if (length <= index) {
    return false;
  }

  if (psdu[index] != 0x04U) {
    return false;
  }

  return macDataRequestPendingCallback_(psdu, length,
                                        macDataRequestPendingContext_);
}

RawRadioLink::RawRadioLink(uint32_t radioBase)
    : radio_(reinterpret_cast<NRF_RADIO_Type*>(static_cast<uintptr_t>(radioBase))),
      radioOwnershipToken_(0U),
      power_(),
      config_(),
      initialized_(false),
      receiverArmed_(false),
      hfxoOwnedByRawRadio_(false),
      txPacket_{0},
      rxPacket_{0} {}

RawRadioLink::~RawRadioLink() {
  if (initialized_ || receiverArmed_ || hfxoOwnedByRawRadio_ ||
      radioOwnershipToken_ != 0U) {
    end();
  }
  if (!rawRadioDisabled(radio_)) {
    power_.quarantineConstantLatencyLease();
  }
  if (radioOwnershipToken_ != 0U) {
    (void)nrf54l15_quarantine_exclusive_radio(
        Nrf54ExclusiveRadioOwner::kProprietary, radioOwnershipToken_);
    nrf54l15_exclusive_radio_fail_stop();
  }
}

bool RawRadioLink::configureProprietary1M() {
  if (radio_ == nullptr) {
    return false;
  }
  if (config_.maxPayloadLength == 0U) {
    return false;
  }

  radio_->SHORTS = 0U;
  radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
  if (!waitRadioStateDisabledBudgeted(radio_, 0U, 200000UL)) {
    return false;
  }
  radio_->TASKS_SOFTRESET = RADIO_TASKS_SOFTRESET_TASKS_SOFTRESET_Trigger;

  radio_->MODE = ((RADIO_MODE_MODE_Nrf_1Mbit << RADIO_MODE_MODE_Pos) &
                  RADIO_MODE_MODE_Msk);
  radio_->TIMING = ((RADIO_TIMING_RU_Fast << RADIO_TIMING_RU_Pos) &
                    RADIO_TIMING_RU_Msk);

  uint32_t pcnf0 = 0U;
  pcnf0 |= (8UL << RADIO_PCNF0_LFLEN_Pos) & RADIO_PCNF0_LFLEN_Msk;
  pcnf0 |= (0UL << RADIO_PCNF0_S0LEN_Pos) & RADIO_PCNF0_S0LEN_Msk;
  pcnf0 |= (0UL << RADIO_PCNF0_S1LEN_Pos) & RADIO_PCNF0_S1LEN_Msk;
  pcnf0 |= (RADIO_PCNF0_S1INCL_Automatic << RADIO_PCNF0_S1INCL_Pos) &
           RADIO_PCNF0_S1INCL_Msk;
  pcnf0 |= (RADIO_PCNF0_PLEN_8bit << RADIO_PCNF0_PLEN_Pos) &
           RADIO_PCNF0_PLEN_Msk;
  pcnf0 |= (RADIO_PCNF0_CRCINC_Exclude << RADIO_PCNF0_CRCINC_Pos) &
           RADIO_PCNF0_CRCINC_Msk;
  radio_->PCNF0 = pcnf0;

  uint32_t pcnf1 = 0U;
  pcnf1 |= (static_cast<uint32_t>(config_.maxPayloadLength)
            << RADIO_PCNF1_MAXLEN_Pos) & RADIO_PCNF1_MAXLEN_Msk;
  pcnf1 |= (0UL << RADIO_PCNF1_STATLEN_Pos) & RADIO_PCNF1_STATLEN_Msk;
  pcnf1 |= (4UL << RADIO_PCNF1_BALEN_Pos) & RADIO_PCNF1_BALEN_Msk;
  pcnf1 |= ((config_.bigEndian ? RADIO_PCNF1_ENDIAN_Big
                               : RADIO_PCNF1_ENDIAN_Little)
            << RADIO_PCNF1_ENDIAN_Pos) & RADIO_PCNF1_ENDIAN_Msk;
  pcnf1 |= ((config_.whiteningEnabled ? RADIO_PCNF1_WHITEEN_Enabled
                                      : RADIO_PCNF1_WHITEEN_Disabled)
            << RADIO_PCNF1_WHITEEN_Pos) & RADIO_PCNF1_WHITEEN_Msk;
  radio_->PCNF1 = pcnf1;

  radio_->BASE0 = (config_.addressBase0 & RADIO_BASE0_BASE0_Msk);
  radio_->PREFIX0 =
      ((static_cast<uint32_t>(config_.addressPrefix0) << RADIO_PREFIX0_AP0_Pos) &
       RADIO_PREFIX0_AP0_Msk);
  radio_->TXADDRESS =
      (0UL << RADIO_TXADDRESS_TXADDRESS_Pos) & RADIO_TXADDRESS_TXADDRESS_Msk;
  radio_->RXADDRESSES =
      (RADIO_RXADDRESSES_ADDR0_Enabled << RADIO_RXADDRESSES_ADDR0_Pos) &
      RADIO_RXADDRESSES_ADDR0_Msk;

  uint32_t crccnf = 0U;
  crccnf |= (RADIO_CRCCNF_LEN_Two << RADIO_CRCCNF_LEN_Pos) &
            RADIO_CRCCNF_LEN_Msk;
  crccnf |= (RADIO_CRCCNF_SKIPADDR_Include << RADIO_CRCCNF_SKIPADDR_Pos) &
            RADIO_CRCCNF_SKIPADDR_Msk;
  radio_->CRCCNF = crccnf;
  radio_->CRCPOLY = (config_.crcPolynomial & RADIO_CRCPOLY_CRCPOLY_Msk);
  radio_->CRCINIT = (config_.crcInit & RADIO_CRCINIT_CRCINIT_Msk);

  return setFrequencyOffsetMhz(config_.frequencyOffsetMhz) &&
         setTxPowerDbm(config_.txPowerDbm);
}

bool RawRadioLink::begin(const RawRadioConfig& config) {
  if (initialized_ || radioOwnershipToken_ != 0U) {
    return false;
  }
  if (radio_ == nullptr || config.maxPayloadLength == 0U) {
    initialized_ = false;
    receiverArmed_ = false;
    return false;
  }

  radioOwnershipToken_ =
      nrf54l15_acquire_exclusive_radio(Nrf54ExclusiveRadioOwner::kProprietary);
  if (radioOwnershipToken_ == 0U) {
    return false;
  }

  const bool hfxoWasRunning = rawRadioHfxoRunning();
  hfxoOwnedByRawRadio_ = hfxoOwnedByRawRadio_ || !hfxoWasRunning;
  if (!ClockControl::startHfxo(true, 1500000UL)) {
    initialized_ = false;
    receiverArmed_ = false;
    releaseOwnedRawRadioHfxo(&hfxoOwnedByRawRadio_);
    (void)releaseExclusiveRadioIfDisabled(Nrf54ExclusiveRadioOwner::kProprietary,
                                          &radioOwnershipToken_, radio_);
    return false;
  }

  config_ = config;
  if (!power_.setLatencyMode(PowerLatencyMode::kConstantLatency)) {
    initialized_ = false;
    receiverArmed_ = false;
    releaseOwnedRawRadioHfxo(&hfxoOwnedByRawRadio_);
    (void)releaseExclusiveRadioIfDisabled(
        Nrf54ExclusiveRadioOwner::kProprietary, &radioOwnershipToken_, radio_);
    return false;
  }

  if (!configureProprietary1M()) {
    initialized_ = false;
    receiverArmed_ = false;
    if (rawRadioDisabled(radio_)) {
      power_.setLatencyMode(PowerLatencyMode::kLowPower);
      releaseOwnedRawRadioHfxo(&hfxoOwnedByRawRadio_);
      (void)releaseExclusiveRadioIfDisabled(
          Nrf54ExclusiveRadioOwner::kProprietary, &radioOwnershipToken_, radio_);
    }
    return false;
  }

  clearRadioCoreEvents(radio_);
  memset(txPacket_, 0, sizeof(txPacket_));
  memset(rxPacket_, 0, sizeof(rxPacket_));
  initialized_ = true;
  receiverArmed_ = false;
  return true;
}

void RawRadioLink::end() {
  if (radioOwnershipToken_ == 0U) {
    initialized_ = false;
    receiverArmed_ = false;
    return;
  }
  if (!nrf54l15_exclusive_radio_is_owned_by(
          Nrf54ExclusiveRadioOwner::kProprietary,
          radioOwnershipToken_)) {
    return;
  }
  if (radio_ == nullptr) {
    initialized_ = false;
    receiverArmed_ = false;
    power_.setLatencyMode(PowerLatencyMode::kLowPower);
    releaseOwnedRawRadioHfxo(&hfxoOwnedByRawRadio_);
    (void)releaseExclusiveRadioIfDisabled(Nrf54ExclusiveRadioOwner::kProprietary,
                                          &radioOwnershipToken_, radio_);
    return;
  }

  radio_->SHORTS = 0U;
  radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
  const bool disabled =
      waitRadioStateDisabledBudgeted(radio_, 0U, 200000UL);
  initialized_ = false;
  receiverArmed_ = false;
  if (disabled) {
    clearRadioCoreEvents(radio_);
    power_.setLatencyMode(PowerLatencyMode::kLowPower);
    releaseOwnedRawRadioHfxo(&hfxoOwnedByRawRadio_);
    (void)releaseExclusiveRadioIfDisabled(Nrf54ExclusiveRadioOwner::kProprietary,
                                          &radioOwnershipToken_, radio_);
  }
}

bool RawRadioLink::setFrequencyOffsetMhz(uint8_t frequencyOffsetMhz) {
  if (radio_ == nullptr ||
      !nrf54l15_exclusive_radio_is_owned_by(
          Nrf54ExclusiveRadioOwner::kProprietary,
          radioOwnershipToken_)) {
    return false;
  }

  config_.frequencyOffsetMhz = frequencyOffsetMhz;
  radio_->FREQUENCY =
      ((static_cast<uint32_t>(config_.frequencyOffsetMhz)
        << RADIO_FREQUENCY_FREQUENCY_Pos) &
       RADIO_FREQUENCY_FREQUENCY_Msk) |
      (0UL << RADIO_FREQUENCY_MAP_Pos);
  return true;
}

bool RawRadioLink::setPipe(uint32_t addressBase0, uint8_t addressPrefix0) {
  if (radio_ == nullptr ||
      !nrf54l15_exclusive_radio_is_owned_by(
          Nrf54ExclusiveRadioOwner::kProprietary,
          radioOwnershipToken_)) {
    return false;
  }

  config_.addressBase0 = addressBase0;
  config_.addressPrefix0 = addressPrefix0;
  radio_->BASE0 = (config_.addressBase0 & RADIO_BASE0_BASE0_Msk);
  radio_->PREFIX0 =
      ((static_cast<uint32_t>(config_.addressPrefix0) << RADIO_PREFIX0_AP0_Pos) &
       RADIO_PREFIX0_AP0_Msk);
  return true;
}

bool RawRadioLink::setTxPowerDbm(int8_t dbm) {
  if (radio_ == nullptr ||
      !nrf54l15_exclusive_radio_is_owned_by(
          Nrf54ExclusiveRadioOwner::kProprietary,
          radioOwnershipToken_)) {
    return false;
  }

  config_.txPowerDbm = dbm;
  const uint32_t regValue = radioTxPowerRegFromDbm(dbm);
  radio_->TXPOWER = ((regValue << RADIO_TXPOWER_TXPOWER_Pos) &
                     RADIO_TXPOWER_TXPOWER_Msk);
  return true;
}

bool RawRadioLink::transmit(const uint8_t* payload, uint8_t length,
                            uint32_t spinLimit) {
  if (!initialized_ || radio_ == nullptr || payload == nullptr) {
    return false;
  }
  if (length == 0U || length > config_.maxPayloadLength) {
    return false;
  }

  txPacket_[0] = length;
  memcpy(&txPacket_[1], payload, length);

  clearRadioCoreEvents(radio_);
  receiverArmed_ = false;
  radio_->PACKETPTR = reinterpret_cast<uint32_t>(txPacket_);
  radio_->SHORTS =
      ((RADIO_SHORTS_TXREADY_START_Enabled << RADIO_SHORTS_TXREADY_START_Pos) &
       RADIO_SHORTS_TXREADY_START_Msk) |
      ((RADIO_SHORTS_PHYEND_DISABLE_Enabled << RADIO_SHORTS_PHYEND_DISABLE_Pos) &
       RADIO_SHORTS_PHYEND_DISABLE_Msk);

  radio_->TASKS_TXEN = RADIO_TASKS_TXEN_TASKS_TXEN_Trigger;
  const bool endSeen = waitRadioEndBudgeted(radio_, 12000U, spinLimit);
  radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
  const bool disabled =
      waitRadioStateDisabledBudgeted(radio_, 3000U, spinLimit);
  radio_->SHORTS = 0U;
  if (disabled) {
    clearRadioCoreEvents(radio_);
  } else {
    initialized_ = false;
  }
  return endSeen && disabled;
}

bool RawRadioLink::armReceive() {
  if (!initialized_ || radio_ == nullptr) {
    return false;
  }

  memset(rxPacket_, 0, sizeof(rxPacket_));
  clearRadioCoreEvents(radio_);
  radio_->PACKETPTR = reinterpret_cast<uint32_t>(rxPacket_);
  radio_->SHORTS =
      ((RADIO_SHORTS_RXREADY_START_Enabled << RADIO_SHORTS_RXREADY_START_Pos) &
       RADIO_SHORTS_RXREADY_START_Msk) |
      ((RADIO_SHORTS_PHYEND_DISABLE_Enabled << RADIO_SHORTS_PHYEND_DISABLE_Pos) &
       RADIO_SHORTS_PHYEND_DISABLE_Msk) |
      ((RADIO_SHORTS_ADDRESS_RSSISTART_Enabled
        << RADIO_SHORTS_ADDRESS_RSSISTART_Pos) &
       RADIO_SHORTS_ADDRESS_RSSISTART_Msk);
  radio_->TASKS_RXEN = RADIO_TASKS_RXEN_TASKS_RXEN_Trigger;
  receiverArmed_ = true;
  return true;
}

RawRadioReceiveStatus RawRadioLink::finishReceive(RawRadioPacket* outPacket,
                                                  uint32_t spinLimit,
                                                  bool timedOut) {
  if (radio_ == nullptr || !receiverArmed_) {
    return RawRadioReceiveStatus::kError;
  }

  const int8_t rssiDbm = radioRssiDbm(radio_);
  const bool disabled =
      waitRadioStateDisabledBudgeted(radio_, 3000U, spinLimit);
  radio_->SHORTS = 0U;
  receiverArmed_ = false;
  if (!disabled) {
    initialized_ = false;
    return RawRadioReceiveStatus::kError;
  }

  if (timedOut ||
      ((radio_->EVENTS_CRCOK == 0U) && (radio_->EVENTS_CRCERROR == 0U))) {
    clearRadioCoreEvents(radio_);
    return RawRadioReceiveStatus::kIdle;
  }

  const uint32_t crcStatus =
      (radio_->CRCSTATUS & RADIO_CRCSTATUS_CRCSTATUS_Msk) >>
      RADIO_CRCSTATUS_CRCSTATUS_Pos;
  const bool crcOkEvent = (radio_->EVENTS_CRCOK != 0U);
  const bool crcOk = crcOkEvent || (crcStatus == RADIO_CRCSTATUS_CRCSTATUS_CRCOk);
  if (!crcOk) {
    clearRadioCoreEvents(radio_);
    return RawRadioReceiveStatus::kCrcError;
  }

  const uint8_t length = minU8(rxPacket_[0], config_.maxPayloadLength);
  if (length == 0U) {
    clearRadioCoreEvents(radio_);
    return RawRadioReceiveStatus::kError;
  }

  if (outPacket != nullptr) {
    outPacket->length = length;
    outPacket->rssiDbm = rssiDbm;
    memcpy(outPacket->payload, &rxPacket_[1], length);
  }

  clearRadioCoreEvents(radio_);
  return RawRadioReceiveStatus::kPacket;
}

RawRadioReceiveStatus RawRadioLink::pollReceive(RawRadioPacket* outPacket,
                                                uint32_t spinLimit) {
  if (!initialized_ || radio_ == nullptr) {
    return RawRadioReceiveStatus::kError;
  }
  if (!receiverArmed_) {
    return RawRadioReceiveStatus::kIdle;
  }

  if ((radio_->EVENTS_CRCOK == 0U) && (radio_->EVENTS_CRCERROR == 0U) &&
      (radio_->EVENTS_END == 0U)) {
    return RawRadioReceiveStatus::kIdle;
  }

  return finishReceive(outPacket, spinLimit, false);
}

RawRadioReceiveStatus RawRadioLink::waitForReceive(RawRadioPacket* outPacket,
                                                   uint32_t listenWindowUs,
                                                   uint32_t spinLimit) {
  if (!initialized_ || radio_ == nullptr) {
    return RawRadioReceiveStatus::kError;
  }
  if (!receiverArmed_ && !armReceive()) {
    return RawRadioReceiveStatus::kError;
  }

  const bool rxDone = waitRadioRxDoneBudgeted(radio_, listenWindowUs, spinLimit);
  if (!rxDone) {
    radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
  }

  return finishReceive(outPacket, spinLimit, !rxDone);
}

bool RawRadioLink::initialized() const { return initialized_; }

bool RawRadioLink::receiverArmed() const { return receiverArmed_; }

uint8_t RawRadioLink::maxPayloadLength() const { return config_.maxPayloadLength; }

const RawRadioConfig& RawRadioLink::config() const { return config_; }

bool ZigbeeRadio::buildDataFrameShort(uint8_t sequence, uint16_t panId,
                                      uint16_t destinationShort,
                                      uint16_t sourceShort, const uint8_t* payload,
                                      uint8_t payloadLength, uint8_t* outPsdu,
                                      uint8_t* outLength, bool requestAck) {
  if (outPsdu == nullptr || outLength == nullptr) {
    return false;
  }
  if (payloadLength > 0U && payload == nullptr) {
    return false;
  }

  // Data frame, PAN compression, destination short, source short.
  // Header layout: FCF(2), Seq(1), DstPAN(2), DstShort(2), SrcShort(2).
  constexpr uint8_t kHeaderLen = 9U;
  if (payloadLength > static_cast<uint8_t>(127U - kHeaderLen)) {
    return false;
  }

  uint16_t frameControl = 0x9841U;
  if (requestAck) {
    frameControl |= (1U << 5U);
  }

  writeLe16(&outPsdu[0], frameControl);
  outPsdu[2] = sequence;
  writeLe16(&outPsdu[3], panId);
  writeLe16(&outPsdu[5], destinationShort);
  writeLe16(&outPsdu[7], sourceShort);
  if (payloadLength > 0U) {
    memcpy(&outPsdu[kHeaderLen], payload, payloadLength);
  }
  *outLength = static_cast<uint8_t>(kHeaderLen + payloadLength);
  return true;
}

bool ZigbeeRadio::buildMacCommandFrameShort(
    uint8_t sequence, uint16_t panId, uint16_t destinationShort,
    uint16_t sourceShort, uint8_t commandId, const uint8_t* payload,
    uint8_t payloadLength, uint8_t* outPsdu, uint8_t* outLength,
    bool requestAck) {
  if (outPsdu == nullptr || outLength == nullptr) {
    return false;
  }
  if (payloadLength > 0U && payload == nullptr) {
    return false;
  }

  // MAC command frame with PAN compression, destination short, source short.
  // Header layout: FCF(2), Seq(1), DstPAN(2), DstShort(2), SrcShort(2),
  // CommandId(1).
  constexpr uint8_t kHeaderLen = 10U;
  if (payloadLength > static_cast<uint8_t>(127U - kHeaderLen)) {
    return false;
  }

  uint16_t frameControl = 0x9843U;
  if (requestAck) {
    frameControl |= (1U << 5U);
  }

  writeLe16(&outPsdu[0], frameControl);
  outPsdu[2] = sequence;
  writeLe16(&outPsdu[3], panId);
  writeLe16(&outPsdu[5], destinationShort);
  writeLe16(&outPsdu[7], sourceShort);
  outPsdu[9] = commandId;
  if (payloadLength > 0U) {
    memcpy(&outPsdu[kHeaderLen], payload, payloadLength);
  }
  *outLength = static_cast<uint8_t>(kHeaderLen + payloadLength);
  return true;
}

bool ZigbeeRadio::parseDataFrameShort(const uint8_t* psdu, uint8_t length,
                                      ZigbeeDataFrameView* outView) {
  if (outView != nullptr) {
    memset(outView, 0, sizeof(*outView));
  }
  if (psdu == nullptr || outView == nullptr || length < 9U) {
    return false;
  }

  const uint16_t frameControl = readLe16(&psdu[0]);
  const uint8_t frameType = static_cast<uint8_t>(frameControl & 0x0007U);
  const bool panCompression = ((frameControl >> 6U) & 0x1U) != 0U;
  const uint8_t dstAddrMode = static_cast<uint8_t>((frameControl >> 10U) & 0x03U);
  const uint8_t srcAddrMode = static_cast<uint8_t>((frameControl >> 14U) & 0x03U);

  if (frameType != 0x01U || dstAddrMode != 0x02U || srcAddrMode != 0x02U) {
    return false;
  }

  uint8_t offset = 3U;
  if (length < static_cast<uint8_t>(offset + 2U + 2U)) {
    return false;
  }
  const uint16_t destinationPan = readLe16(&psdu[offset]);
  offset = static_cast<uint8_t>(offset + 2U);
  const uint16_t destinationShort = readLe16(&psdu[offset]);
  offset = static_cast<uint8_t>(offset + 2U);

  uint16_t sourcePan = destinationPan;
  if (!panCompression) {
    if (length < static_cast<uint8_t>(offset + 2U)) {
      return false;
    }
    sourcePan = readLe16(&psdu[offset]);
    offset = static_cast<uint8_t>(offset + 2U);
  }

  if (length < static_cast<uint8_t>(offset + 2U)) {
    return false;
  }
  const uint16_t sourceShort = readLe16(&psdu[offset]);
  offset = static_cast<uint8_t>(offset + 2U);
  if (length < offset) {
    return false;
  }

  outView->valid = true;
  outView->ackRequested = ((frameControl >> 5U) & 0x1U) != 0U;
  outView->sequence = psdu[2];
  outView->panId = sourcePan;
  outView->destinationShort = destinationShort;
  outView->sourceShort = sourceShort;
  outView->payloadLength = static_cast<uint8_t>(length - offset);
  outView->payload = &psdu[offset];
  return true;
}

bool ZigbeeRadio::parseMacCommandFrameShort(const uint8_t* psdu, uint8_t length,
                                            ZigbeeMacCommandView* outView) {
  if (outView != nullptr) {
    memset(outView, 0, sizeof(*outView));
  }
  if (psdu == nullptr || outView == nullptr || length < 10U) {
    return false;
  }

  const uint16_t frameControl = readLe16(&psdu[0]);
  const uint8_t frameType = static_cast<uint8_t>(frameControl & 0x0007U);
  const bool panCompression = ((frameControl >> 6U) & 0x1U) != 0U;
  const uint8_t dstAddrMode = static_cast<uint8_t>((frameControl >> 10U) & 0x03U);
  const uint8_t srcAddrMode = static_cast<uint8_t>((frameControl >> 14U) & 0x03U);

  if (frameType != 0x03U || dstAddrMode != 0x02U || srcAddrMode != 0x02U) {
    return false;
  }

  uint8_t offset = 3U;
  if (length < static_cast<uint8_t>(offset + 2U + 2U)) {
    return false;
  }
  const uint16_t destinationPan = readLe16(&psdu[offset]);
  offset = static_cast<uint8_t>(offset + 2U);
  const uint16_t destinationShort = readLe16(&psdu[offset]);
  offset = static_cast<uint8_t>(offset + 2U);

  uint16_t sourcePan = destinationPan;
  if (!panCompression) {
    if (length < static_cast<uint8_t>(offset + 2U)) {
      return false;
    }
    sourcePan = readLe16(&psdu[offset]);
    offset = static_cast<uint8_t>(offset + 2U);
  }

  if (length < static_cast<uint8_t>(offset + 2U + 1U)) {
    return false;
  }
  const uint16_t sourceShort = readLe16(&psdu[offset]);
  offset = static_cast<uint8_t>(offset + 2U);
  const uint8_t commandId = psdu[offset];
  offset = static_cast<uint8_t>(offset + 1U);

  outView->valid = true;
  outView->ackRequested = ((frameControl >> 5U) & 0x1U) != 0U;
  outView->sequence = psdu[2];
  outView->panId = sourcePan;
  outView->destinationShort = destinationShort;
  outView->sourceShort = sourceShort;
  outView->commandId = commandId;
  outView->payloadLength = static_cast<uint8_t>(length - offset);
  outView->payload = &psdu[offset];
  return true;
}

bool ZigbeeRadio::buildMacAcknowledgement(uint8_t sequence, uint8_t* outPsdu,
                                          uint8_t* outLength,
                                          bool framePending) {
  if (outPsdu == nullptr || outLength == nullptr) {
    return false;
  }

  uint16_t frameControl = 0x0002U;
  if (framePending) {
    frameControl |= (1U << 4U);
  }
  writeLe16(&outPsdu[0], frameControl);
  outPsdu[2] = sequence;
  *outLength = 3U;
  return true;
}

bool ZigbeeRadio::parseMacAcknowledgement(
    const uint8_t* psdu, uint8_t length, ZigbeeMacAcknowledgementView* outView) {
  if (outView != nullptr) {
    memset(outView, 0, sizeof(*outView));
  }
  if (psdu == nullptr || outView == nullptr || length != 3U) {
    return false;
  }

  const uint16_t frameControl = readLe16(&psdu[0]);
  const uint8_t frameType = static_cast<uint8_t>(frameControl & 0x0007U);
  const uint8_t destinationMode =
      static_cast<uint8_t>((frameControl >> 10U) & 0x03U);
  const uint8_t sourceMode =
      static_cast<uint8_t>((frameControl >> 14U) & 0x03U);
  if (frameType != 0x02U || destinationMode != 0U || sourceMode != 0U) {
    return false;
  }

  outView->valid = true;
  outView->framePending = ((frameControl >> 4U) & 0x1U) != 0U;
  outView->sequence = psdu[2];
  return true;
}
