#include "nrf54l15_hal.h"
#include <string.h>

namespace xiao_nrf54l15 {

namespace {

constexpr uint32_t kKmuSlotMax = KMU_KEYSLOT_ID_Max;
constexpr uint32_t kTampcWriteKey =
    (TAMPC_PROTECT_DOMAIN_DBGEN_CTRL_KEY_KEY
     << TAMPC_PROTECT_DOMAIN_DBGEN_CTRL_KEY_Pos);
constexpr uint32_t kTampcWriteProtectionClear =
    (TAMPC_PROTECT_DOMAIN_DBGEN_CTRL_WRITEPROTECTION_Clear
     << TAMPC_PROTECT_DOMAIN_DBGEN_CTRL_WRITEPROTECTION_Pos);
constexpr uint32_t kTampcLockEnabled =
    (TAMPC_PROTECT_DOMAIN_DBGEN_CTRL_LOCK_Enabled
     << TAMPC_PROTECT_DOMAIN_DBGEN_CTRL_LOCK_Pos);

inline bool validKmuSlot(uint8_t slot) {
  return slot <= kKmuSlotMax;
}

bool waitRramcReady(NRF_RRAMC_Type* rramc, uint32_t spinLimit) {
  if (rramc == nullptr) {
    return false;
  }
  while (spinLimit-- > 0U) {
    if (((rramc->READY & RRAMC_READY_READY_Msk) >> RRAMC_READY_READY_Pos) ==
        RRAMC_READY_READY_Ready) {
      return true;
    }
  }
  return false;
}

constexpr bool kSeedStateManagedByKmu =
#if defined(NRF54LM20A_XXAA) || defined(NRF54LM20B_XXAA)
    true;
#else
    false;
#endif

}  // namespace

Kmu::Kmu(uint32_t base, uint32_t rramcBase)
    : kmu_(reinterpret_cast<NRF_KMU_Type*>(base)),
      rramc_(reinterpret_cast<NRF_RRAMC_Type*>(rramcBase)) {}

bool Kmu::ready() const {
#if defined(NRF_TRUSTZONE_NONSECURE)
  return false;
#else
  if (kmu_ == nullptr) {
    return false;
  }
  return ((kmu_->STATUS & KMU_STATUS_STATUS_Msk) >> KMU_STATUS_STATUS_Pos) ==
         KMU_STATUS_STATUS_Ready;
#endif
}

void Kmu::clearEvents() {
  if (kmu_ == nullptr) {
    return;
  }
  kmu_->EVENTS_PROVISIONED = 0U;
  kmu_->EVENTS_PUSHED = 0U;
  kmu_->EVENTS_REVOKED = 0U;
  kmu_->EVENTS_ERROR = 0U;
  kmu_->EVENTS_METADATAREAD = 0U;
  kmu_->EVENTS_PUSHBLOCKED = 0U;
}

bool Kmu::pollProvisioned(bool clearEvent) {
  if (kmu_ == nullptr) {
    return false;
  }
  const bool signaled = ((kmu_->EVENTS_PROVISIONED &
                          KMU_EVENTS_PROVISIONED_EVENTS_PROVISIONED_Msk) >>
                         KMU_EVENTS_PROVISIONED_EVENTS_PROVISIONED_Pos) ==
                        KMU_EVENTS_PROVISIONED_EVENTS_PROVISIONED_Generated;
  if (signaled && clearEvent) {
    kmu_->EVENTS_PROVISIONED = 0U;
  }
  return signaled;
}

bool Kmu::pollPushed(bool clearEvent) {
  if (kmu_ == nullptr) {
    return false;
  }
  const bool signaled =
      ((kmu_->EVENTS_PUSHED & KMU_EVENTS_PUSHED_EVENTS_PUSHED_Msk) >>
       KMU_EVENTS_PUSHED_EVENTS_PUSHED_Pos) ==
      KMU_EVENTS_PUSHED_EVENTS_PUSHED_Generated;
  if (signaled && clearEvent) {
    kmu_->EVENTS_PUSHED = 0U;
  }
  return signaled;
}

bool Kmu::pollRevoked(bool clearEvent) {
  if (kmu_ == nullptr) {
    return false;
  }
  const bool signaled =
      ((kmu_->EVENTS_REVOKED & KMU_EVENTS_REVOKED_EVENTS_REVOKED_Msk) >>
       KMU_EVENTS_REVOKED_EVENTS_REVOKED_Pos) ==
      KMU_EVENTS_REVOKED_EVENTS_REVOKED_Generated;
  if (signaled && clearEvent) {
    kmu_->EVENTS_REVOKED = 0U;
  }
  return signaled;
}

bool Kmu::pollMetadataRead(bool clearEvent) {
  if (kmu_ == nullptr) {
    return false;
  }
  const bool signaled =
      ((kmu_->EVENTS_METADATAREAD &
        KMU_EVENTS_METADATAREAD_EVENTS_METADATAREAD_Msk) >>
       KMU_EVENTS_METADATAREAD_EVENTS_METADATAREAD_Pos) ==
      KMU_EVENTS_METADATAREAD_EVENTS_METADATAREAD_Generated;
  if (signaled && clearEvent) {
    kmu_->EVENTS_METADATAREAD = 0U;
  }
  return signaled;
}

bool Kmu::pollPushBlocked(bool clearEvent) {
  if (kmu_ == nullptr) {
    return false;
  }
  const bool signaled =
      ((kmu_->EVENTS_PUSHBLOCKED &
        KMU_EVENTS_PUSHBLOCKED_EVENTS_PUSHBLOCKED_Msk) >>
       KMU_EVENTS_PUSHBLOCKED_EVENTS_PUSHBLOCKED_Pos) ==
      KMU_EVENTS_PUSHBLOCKED_EVENTS_PUSHBLOCKED_Generated;
  if (signaled && clearEvent) {
    kmu_->EVENTS_PUSHBLOCKED = 0U;
  }
  return signaled;
}

bool Kmu::pollError(bool clearEvent) {
  if (kmu_ == nullptr) {
    return false;
  }
  const bool signaled =
      ((kmu_->EVENTS_ERROR & KMU_EVENTS_ERROR_EVENTS_ERROR_Msk) >>
       KMU_EVENTS_ERROR_EVENTS_ERROR_Pos) ==
      KMU_EVENTS_ERROR_EVENTS_ERROR_Generated;
  if (signaled && clearEvent) {
    kmu_->EVENTS_ERROR = 0U;
  }
  return signaled;
}

bool Kmu::waitReady(uint32_t spinLimit) const {
  if (kmu_ == nullptr) {
    return false;
  }
  while (spinLimit-- > 0U) {
    if (ready()) {
      return true;
    }
  }
  return false;
}

bool Kmu::enableRramWrite(uint32_t* previousConfig, uint32_t spinLimit) const {
  if (previousConfig == nullptr || rramc_ == nullptr) {
    return false;
  }
  if (!waitRramcReady(rramc_, spinLimit)) {
    return false;
  }
  *previousConfig = rramc_->CONFIG;

  // Do not switch write-buffer policy with pending bytes. Commit the current
  // buffer first, then force unbuffered writes for the KMU provisioning task.
  if ((*previousConfig & RRAMC_CONFIG_WRITEBUFSIZE_Msk) != 0U) {
    rramc_->EVENTS_READY = 0U;
    rramc_->TASKS_COMMITWRITEBUF =
        RRAMC_TASKS_COMMITWRITEBUF_TASKS_COMMITWRITEBUF_Trigger;
    if (!waitRramcReady(rramc_, spinLimit)) {
      return false;
    }
  }

  rramc_->CONFIG =
      (*previousConfig & ~RRAMC_CONFIG_WRITEBUFSIZE_Msk) |
      RRAMC_CONFIG_WEN_Msk;
  return waitRramcReady(rramc_, spinLimit);
}

void Kmu::restoreRramWrite(uint32_t previousConfig, uint32_t spinLimit) const {
  if (rramc_ == nullptr) {
    return;
  }
  (void)waitRramcReady(rramc_, spinLimit);
  rramc_->CONFIG = previousConfig;
  (void)waitRramcReady(rramc_, spinLimit);
}

bool Kmu::performSimpleTask(uint8_t slot,
                            volatile uint32_t& task,
                            volatile uint32_t& successEvent,
                            uint32_t spinLimit) {
#if defined(NRF_TRUSTZONE_NONSECURE)
  (void)slot;
  (void)task;
  (void)successEvent;
  (void)spinLimit;
  return false;
#else
  if (kmu_ == nullptr || !validKmuSlot(slot) || !waitReady(spinLimit)) {
    return false;
  }

  clearEvents();
  kmu_->KEYSLOT = static_cast<uint32_t>(slot);
  successEvent = 0U;
  kmu_->EVENTS_ERROR = 0U;
  task = 1U;

  while (spinLimit-- > 0U) {
    if ((successEvent & 1U) != 0U) {
      return true;
    }
    if ((kmu_->EVENTS_ERROR & 1U) != 0U) {
      return false;
    }
  }
  return false;
#endif
}

bool Kmu::readMetadata(uint8_t slot, uint32_t* metadata, uint32_t spinLimit) {
#if defined(NRF_TRUSTZONE_NONSECURE)
  (void)slot;
  (void)metadata;
  (void)spinLimit;
  return false;
#else
  if (metadata == nullptr) {
    return false;
  }
  if (!performSimpleTask(slot, kmu_->TASKS_READMETADATA,
                         kmu_->EVENTS_METADATAREAD, spinLimit)) {
    return false;
  }
  *metadata = kmu_->METADATA;
  return true;
#endif
}

bool Kmu::provision(uint8_t slot, const KmuProvisionSource& source,
                    uint32_t spinLimit) {
#if defined(NRF_TRUSTZONE_NONSECURE)
  (void)slot;
  (void)source;
  (void)spinLimit;
  return false;
#else
  if (kmu_ == nullptr || !validKmuSlot(slot) || !waitReady(spinLimit)) {
    return false;
  }
  if ((reinterpret_cast<uintptr_t>(&source) & 0xFUL) != 0U ||
      (source.destination & 0xFUL) != 0U) {
    return false;
  }

  uint32_t previousConfig = 0U;
  if (!enableRramWrite(&previousConfig, spinLimit)) {
    return false;
  }

  clearEvents();
  kmu_->KEYSLOT = static_cast<uint32_t>(slot);
  kmu_->SRC = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&source));
  kmu_->TASKS_PROVISION = KMU_TASKS_PROVISION_TASKS_PROVISION_Trigger;

  bool ok = false;
  while (spinLimit-- > 0U) {
    if ((kmu_->EVENTS_PROVISIONED & 1U) != 0U) {
      ok = true;
      break;
    }
    if ((kmu_->EVENTS_ERROR & 1U) != 0U) {
      break;
    }
  }

  restoreRramWrite(previousConfig, 600000UL);
  return ok;
#endif
}

bool Kmu::push(uint8_t slot, uint32_t spinLimit) {
  return performSimpleTask(slot, kmu_->TASKS_PUSH, kmu_->EVENTS_PUSHED,
                           spinLimit);
}

bool Kmu::revoke(uint8_t slot, uint32_t spinLimit) {
  return performSimpleTask(slot, kmu_->TASKS_REVOKE, kmu_->EVENTS_REVOKED,
                           spinLimit);
}

bool Kmu::pushBlock(uint8_t slot, uint32_t spinLimit) {
  return performSimpleTask(slot, kmu_->TASKS_PUSHBLOCK,
                           kmu_->EVENTS_PUSHBLOCKED, spinLimit);
}

CracenIkg::CracenIkg(uint32_t controlBase, uint32_t coreBase)
    : cracen_(reinterpret_cast<NRF_CRACEN_Type*>(controlBase)),
      core_(reinterpret_cast<NRF_CRACENCORE_Type*>(coreBase)),
      operandRamBase_(coreBase == 0U
                          ? 0U
                          : static_cast<uintptr_t>(coreBase) +
                                kPkOperandRamOffset),
      active_(false) {}

bool CracenIkg::begin(uint32_t spinLimit) {
#if defined(NRF_TRUSTZONE_NONSECURE)
  (void)spinLimit;
  return false;
#else
  if (cracen_ == nullptr || core_ == nullptr) {
    return false;
  }

  cracen_->ENABLE |=
      CRACEN_ENABLE_PKEIKG_Msk | CRACEN_ENABLE_CRYPTOMASTER_Msk;
  clearEvent();
  active_ = waitReady(spinLimit);
  if (!active_) {
    cracen_->ENABLE &=
        ~(CRACEN_ENABLE_PKEIKG_Msk | CRACEN_ENABLE_CRYPTOMASTER_Msk);
  }
  return active_;
#endif
}

void CracenIkg::end() {
#if defined(NRF_TRUSTZONE_NONSECURE)
  active_ = false;
#else
  if (cracen_ == nullptr) {
    active_ = false;
    return;
  }
  cracen_->ENABLE &= ~(CRACEN_ENABLE_PKEIKG_Msk | CRACEN_ENABLE_CRYPTOMASTER_Msk);
  clearEvent();
  active_ = false;
#endif
}

bool CracenIkg::active() const { return active_; }

void CracenIkg::clearEvent() {
  if (cracen_ == nullptr) {
    return;
  }
  cracen_->EVENTS_PKEIKG = 0U;
}

uint32_t CracenIkg::status() const {
  return (core_ == nullptr) ? 0xFFFFFFFFUL : core_->IKG.STATUS;
}

uint32_t CracenIkg::pkeStatus() const {
  return (core_ == nullptr) ? 0xFFFFFFFFUL : core_->IKG.PKESTATUS;
}

uint32_t CracenIkg::hwConfig() const {
  return (core_ == nullptr) ? 0xFFFFFFFFUL : core_->IKG.HWCONFIG;
}

uint8_t CracenIkg::symmetricKeyCapacity() const {
  const uint32_t config = hwConfig();
  if (config == 0xFFFFFFFFUL) {
    return 0U;
  }
  return static_cast<uint8_t>(
      (config & CRACENCORE_IKG_HWCONFIG_NBSYMKEYS_Msk) >>
      CRACENCORE_IKG_HWCONFIG_NBSYMKEYS_Pos);
}

uint8_t CracenIkg::privateKeyCapacity() const {
  const uint32_t config = hwConfig();
  if (config == 0xFFFFFFFFUL) {
    return 0U;
  }
  return static_cast<uint8_t>(
      (config & CRACENCORE_IKG_HWCONFIG_NBPRIVKEYS_Msk) >>
      CRACENCORE_IKG_HWCONFIG_NBPRIVKEYS_Pos);
}

bool CracenIkg::okay() const {
  return core_ != nullptr &&
         (status() & CRACENCORE_IKG_STATUS_OKAY_Msk) != 0U;
}

bool CracenIkg::seedError() const {
  return (status() & CRACENCORE_IKG_STATUS_SEEDERROR_Msk) != 0U;
}

bool CracenIkg::entropyError() const {
  return (status() & CRACENCORE_IKG_STATUS_ENTROPYERROR_Msk) != 0U;
}

bool CracenIkg::catastrophicError() const {
  return (status() & CRACENCORE_IKG_STATUS_CATASTROPHICERROR_Msk) != 0U;
}

bool CracenIkg::ctrDrbgBusy() const {
  return (status() & CRACENCORE_IKG_STATUS_CTRDRBGBUSY_Msk) != 0U;
}

bool CracenIkg::symmetricKeysStored() const {
  return core_ != nullptr &&
         (status() & CRACENCORE_IKG_STATUS_SYMKEYSTORED_Msk) != 0U;
}

bool CracenIkg::privateKeysStored() const {
  return core_ != nullptr &&
         (status() & CRACENCORE_IKG_STATUS_PRIVKEYSTORED_Msk) != 0U;
}

bool CracenIkg::seedValid() const {
  if (cracen_ == nullptr) {
    return false;
  }
  return (cracen_->SEEDVALID & CRACEN_SEEDVALID_VALID_Msk) ==
         (CRACEN_SEEDVALID_VALID_Enabled << CRACEN_SEEDVALID_VALID_Pos);
}

bool CracenIkg::seedLocked() const {
  if (cracen_ == nullptr) {
    return false;
  }
  return (cracen_->SEEDLOCK & CRACEN_SEEDLOCK_ENABLE_Msk) ==
         (CRACEN_SEEDLOCK_ENABLE_Enabled << CRACEN_SEEDLOCK_ENABLE_Pos);
}

bool CracenIkg::seedStateManagedByKmu() const {
  return kSeedStateManagedByKmu;
}

bool CracenIkg::markSeedValid(bool valid) {
#if defined(NRF_TRUSTZONE_NONSECURE)
  (void)valid;
  return false;
#else
  // LM20 seed validity is asserted by the KMU after all seed words are
  // pushed. Its SEEDVALID register is read-only in practice. On L15, this
  // raw API cannot prove that a complete nonzero KMU seed was pushed, so it
  // deliberately refuses to bless the current contents.
  if (cracen_ == nullptr || kSeedStateManagedByKmu || valid || seedLocked()) {
    return false;
  }
  cracen_->SEEDVALID =
      CRACEN_SEEDVALID_VALID_Disabled << CRACEN_SEEDVALID_VALID_Pos;
  __asm volatile("dsb 0xF" ::: "memory");
  return !seedValid();
#endif
}

bool CracenIkg::lockSeed() {
#if defined(NRF_TRUSTZONE_NONSECURE)
  return false;
#else
  // On LM20 the KMU owns SEEDLOCK and software writes have no effect. On
  // L15, writing SEEDLOCK while SEEDVALID is clear would itself validate an
  // unknown seed, so require an already-valid seed and verify the readback.
  if (cracen_ == nullptr || kSeedStateManagedByKmu || !seedValid()) {
    return false;
  }
  if (seedLocked()) {
    return true;
  }
  cracen_->SEEDLOCK =
      CRACEN_SEEDLOCK_ENABLE_Enabled << CRACEN_SEEDLOCK_ENABLE_Pos;
  __asm volatile("dsb 0xF" ::: "memory");
  return seedLocked();
#endif
}

bool CracenIkg::lockProtectedRam() {
#if defined(NRF_TRUSTZONE_NONSECURE)
  return false;
#else
  if (cracen_ == nullptr || !active_) {
    return false;
  }
  cracen_->PROTECTEDRAMLOCK = CRACEN_PROTECTEDRAMLOCK_ENABLE_Enabled
                              << CRACEN_PROTECTEDRAMLOCK_ENABLE_Pos;
  __asm volatile("dsb 0xF" ::: "memory");
  return (cracen_->PROTECTEDRAMLOCK &
          CRACEN_PROTECTEDRAMLOCK_ENABLE_Msk) ==
         (CRACEN_PROTECTEDRAMLOCK_ENABLE_Enabled
          << CRACEN_PROTECTEDRAMLOCK_ENABLE_Pos);
#endif
}

bool CracenIkg::softResetKeys(uint32_t spinLimit) {
#if defined(NRF_TRUSTZONE_NONSECURE)
  (void)spinLimit;
  return false;
#else
  if (core_ == nullptr || !active_) {
    return false;
  }
  core_->IKG.SOFTRST = CRACENCORE_IKG_SOFTRST_SOFTRST_KEY;
  core_->IKG.SOFTRST = CRACENCORE_IKG_SOFTRST_SOFTRST_NORMAL;
  return waitReady(spinLimit);
#endif
}

bool CracenIkg::initInput() {
#if defined(NRF_TRUSTZONE_NONSECURE)
  return false;
#else
  if (core_ == nullptr || !active_) {
    return false;
  }
  core_->IKG.INITDATA = CRACENCORE_IKG_INITDATA_INITDATA_Msk;
  return true;
#endif
}

bool CracenIkg::writeNonce(const uint32_t* words, size_t wordCount) {
#if defined(NRF_TRUSTZONE_NONSECURE)
  (void)words;
  (void)wordCount;
  return false;
#else
  const size_t maxWords =
      static_cast<size_t>((hwConfig() &
                           CRACENCORE_IKG_HWCONFIG_NONCELENGTH_Msk) >>
                          CRACENCORE_IKG_HWCONFIG_NONCELENGTH_Pos);
  if (core_ == nullptr || !active_ || wordCount > maxWords ||
      (wordCount != 0U && words == nullptr)) {
    return false;
  }
  for (size_t i = 0; i < wordCount; ++i) {
    core_->IKG.NONCE = words[i];
  }
  return true;
#endif
}

bool CracenIkg::writePersonalization(const uint32_t* words, size_t wordCount) {
#if defined(NRF_TRUSTZONE_NONSECURE)
  (void)words;
  (void)wordCount;
  return false;
#else
  const size_t maxWords = static_cast<size_t>(
      (hwConfig() &
       CRACENCORE_IKG_HWCONFIG_PERSONALIZATIONSTRINGLENGTH_Msk) >>
      CRACENCORE_IKG_HWCONFIG_PERSONALIZATIONSTRINGLENGTH_Pos);
  if (core_ == nullptr || !active_ || wordCount > maxWords ||
      (wordCount != 0U && words == nullptr)) {
    return false;
  }
  for (size_t i = 0; i < wordCount; ++i) {
    core_->IKG.PERSONALISATIONSTRING = words[i];
  }
  return true;
#endif
}

bool CracenIkg::setReseedInterval(uint64_t interval) {
#if defined(NRF_TRUSTZONE_NONSECURE)
  (void)interval;
  return false;
#else
  if (core_ == nullptr || !active_ || (interval >> 48U) != 0U) {
    return false;
  }
  core_->IKG.RESEEDINTERVALLSB = static_cast<uint32_t>(interval & 0xFFFFFFFFULL);
  core_->IKG.RESEEDINTERVALMSB =
      static_cast<uint32_t>((interval >> 32) & 0xFFFFULL);
  return true;
#endif
}

bool CracenIkg::start(uint32_t spinLimit) {
#if defined(NRF_TRUSTZONE_NONSECURE)
  (void)spinLimit;
  return false;
#else
  if (!active_ && !begin(spinLimit)) {
    return false;
  }
  if (core_ == nullptr || !active_ || !seedValid()) {
    return false;
  }

  clearEvent();
  core_->IKG.START = CRACENCORE_IKG_START_START_Msk;
  return waitGenerationComplete(spinLimit);
#endif
}

bool CracenIkg::waitReady(uint32_t spinLimit) const {
  if (core_ == nullptr) {
    return false;
  }
  while (spinLimit-- > 0U) {
    const uint32_t ikgStatus = core_->IKG.STATUS;
    const uint32_t ikgPkeStatus = core_->IKG.PKESTATUS;
    const uint32_t pkStatus = core_->PK.STATUS;
    if ((ikgStatus & CRACENCORE_IKG_STATUS_CATASTROPHICERROR_Msk) != 0U) {
      return false;
    }
    if ((ikgStatus & CRACENCORE_IKG_STATUS_CTRDRBGBUSY_Msk) == 0U &&
        (ikgPkeStatus & CRACENCORE_IKG_PKESTATUS_ERASEBUSY_Msk) == 0U &&
        (pkStatus & CRACENCORE_PK_STATUS_PKBUSY_Msk) == 0U) {
      return true;
    }
  }
  return false;
}

bool CracenIkg::waitGenerationComplete(uint32_t spinLimit) const {
  if (core_ == nullptr) {
    return false;
  }
  while (spinLimit-- > 0U) {
    const uint32_t currentStatus = core_->IKG.STATUS;
    if ((currentStatus & (CRACENCORE_IKG_STATUS_SEEDERROR_Msk |
                          CRACENCORE_IKG_STATUS_ENTROPYERROR_Msk |
                          CRACENCORE_IKG_STATUS_CATASTROPHICERROR_Msk)) != 0U) {
      return false;
    }
    if ((currentStatus & CRACENCORE_IKG_STATUS_OKAY_Msk) != 0U &&
        (currentStatus & CRACENCORE_IKG_STATUS_CTRDRBGBUSY_Msk) == 0U) {
      return true;
    }
  }
  return false;
}

// ─── PKE / PK Engine direct access ───────────────────────────

bool CracenIkg::pkStart() {
  if (core_ == nullptr || !active_) return false;
  core_->PK.CONTROL = CRACENCORE_PK_CONTROL_CLEARIRQ_Msk;
  __asm volatile("dsb 0xF" ::: "memory");
  core_->PK.CONTROL = CRACENCORE_PK_CONTROL_START_Msk;
  return true;
}

bool CracenIkg::pkBusy() const {
  if (core_ == nullptr || !active_) return true;
  return (core_->PK.STATUS & CRACENCORE_PK_STATUS_PKBUSY_Msk) != 0U;
}

void CracenIkg::pkClearIrq() {
  if (core_ == nullptr || !active_) return;
  core_->PK.CONTROL = CRACENCORE_PK_CONTROL_CLEARIRQ_Msk;
}

void CracenIkg::pkSetCommand(uint32_t cmd) {
  if (core_ == nullptr || !active_) return;
  core_->PK.COMMAND = cmd;
}

void CracenIkg::pkSetPointers(uint8_t a, uint8_t b, uint8_t c, uint8_t n) {
  if (core_ == nullptr || !active_) return;
  uint32_t ptrs = ((uint32_t)(a & 0xF) << CRACENCORE_PK_POINTERS_OPPTRA_Pos) |
                  ((uint32_t)(b & 0xF) << CRACENCORE_PK_POINTERS_OPPTRB_Pos) |
                  ((uint32_t)(c & 0xF) << CRACENCORE_PK_POINTERS_OPPTRC_Pos) |
                  ((uint32_t)(n & 0xF) << CRACENCORE_PK_POINTERS_OPPTRN_Pos);
  core_->PK.POINTERS = ptrs;
}

void CracenIkg::pkSetOpsize(uint32_t size) {
  if (core_ == nullptr || !active_) return;
  core_->PK.OPSIZE = size;
}

bool CracenIkg::operandAccessAllowed(int slot, const void* data,
                                    size_t len) const {
  if (core_ == nullptr || cracen_ == nullptr || !active_ ||
      operandRamBase_ == 0U || data == nullptr || len == 0U ||
      len > kPkOperandSlotSize || slot < 0 ||
      slot >= static_cast<int>(kPkOperandSlotCount)) {
    return false;
  }
  // Once isolated keys exist, hardware Secure Mode only exposes pages 8-12
  // to the CPU. Reject the other pages before the bus can fault.
  return !privateKeysStored() || (slot >= 8 && slot <= 12);
}

bool CracenIkg::pkWriteOperand(int slot, const uint8_t* data, size_t len) {
  if (!operandAccessAllowed(slot, data, len)) {
    return false;
  }
  volatile uint32_t* pkRam = reinterpret_cast<volatile uint32_t*>(
      operandRamBase_ + static_cast<uintptr_t>(slot) * kPkOperandSlotSize);
  const size_t words = (len + 3U) / 4U;
  for (size_t i = 0; i < words; ++i) {
    uint32_t value = 0U;
    for (size_t byte = 0; byte < 4U && i * 4U + byte < len; ++byte) {
      value |= static_cast<uint32_t>(data[i * 4U + byte]) << (byte * 8U);
    }
    pkRam[i] = value;
  }
  __asm volatile("dsb 0xF" ::: "memory");
  return true;
}

bool CracenIkg::pkReadOperand(int slot, uint8_t* data, size_t len) {
  if (!operandAccessAllowed(slot, data, len)) {
    return false;
  }
  __asm volatile("dsb 0xF" ::: "memory");
  volatile const uint32_t* pkRam = reinterpret_cast<volatile const uint32_t*>(
      operandRamBase_ + static_cast<uintptr_t>(slot) * kPkOperandSlotSize);
  const size_t words = (len + 3U) / 4U;
  for (size_t i = 0; i < words; ++i) {
    const uint32_t value = pkRam[i];
    for (size_t byte = 0; byte < 4U && i * 4U + byte < len; ++byte) {
      data[i * 4U + byte] =
          static_cast<uint8_t>(value >> (byte * 8U));
    }
  }
  return true;
}

bool CracenIkg::pkWaitComplete(uint32_t spinLimit) {
  if (core_ == nullptr || !active_) {
    return false;
  }
  while (spinLimit-- > 0U) {
    const uint32_t currentStatus = core_->PK.STATUS;
    if ((currentStatus & CRACENCORE_PK_STATUS_ERRORFLAGS_Msk) != 0U) {
      return false;
    }
    if ((currentStatus & CRACENCORE_PK_STATUS_PKBUSY_Msk) == 0U &&
        (currentStatus & CRACENCORE_PK_STATUS_INTRPTSTATUS_Msk) != 0U) {
      return true;
    }
  }
  return false;
}

// ─── IKG high-level ECC operations ─────────────────────────────

bool CracenIkg::ikgGenerateKey() {
  if (core_ == nullptr || !active_ || !seedValid()) {
    return false;
  }
  if (!softResetKeys(1000000UL) || !seedValid() || !initInput()) {
    return false;
  }
  return start(1000000UL) && privateKeysStored();
}

bool CracenIkg::ikgEcdsaSign(const uint8_t hash[32]) {
  (void)hash;
  return false;
}

bool CracenIkg::ikgPointMul(const uint8_t scalar[32], const uint8_t pointX[32], const uint8_t pointY[32]) {
  (void)scalar;
  (void)pointX;
  (void)pointY;
  return false;
}

bool CracenIkg::ikgReadPublicKey(uint8_t pubKey[65]) {
  if (pubKey != nullptr) {
    memset(pubKey, 0, 65U);
  }
  return false;
}

bool CracenIkg::ikgReadEcdsaSignature(uint8_t r[32], uint8_t s[32]) {
  if (r != nullptr) {
    memset(r, 0, 32U);
  }
  if (s != nullptr) {
    memset(s, 0, 32U);
  }
  return false;
}

bool CracenIkg::ikgReadPointMulResult(uint8_t x[32], uint8_t y[32]) {
  if (x != nullptr) {
    memset(x, 0, 32U);
  }
  if (y != nullptr) {
    memset(y, 0, 32U);
  }
  return false;
}

uint32_t CracenIkg::pkStatus() const {
  return (core_ == nullptr) ? 0xFFFFFFFFUL : core_->PK.STATUS;
}

uint32_t CracenIkg::ikgPkeStatus() const {
  return (core_ == nullptr) ? 0xFFFFFFFFUL : core_->IKG.PKESTATUS;
}

uint32_t CracenIkg::ikgStatus() const {
  return (core_ == nullptr) ? 0xFFFFFFFFUL : core_->IKG.STATUS;
}

uint32_t CracenIkg::pkCommand() const {
  return (core_ == nullptr) ? 0xFFFFFFFFUL : core_->PK.COMMAND;
}

bool CracenIkg::pkConfigureP256() {
  if (core_ == nullptr || !active_) return false;
  // Only set curve and size fields — OPEADDR is set by IKG operation.
  // Read current command to preserve OPEADDR (default 0xF is fine)
  uint32_t cmd = core_->PK.COMMAND;
  // Clear fields we're setting
  cmd &= ~(CRACENCORE_PK_COMMAND_SELCURVE_Msk |
           CRACENCORE_PK_COMMAND_OPBYTESM1_Msk |
           CRACENCORE_PK_COMMAND_FIELDF_Msk);
  // Set P-256 configuration
  cmd |= (CRACENCORE_PK_COMMAND_SELCURVE_P256 << CRACENCORE_PK_COMMAND_SELCURVE_Pos);
  cmd |= (31U << CRACENCORE_PK_COMMAND_OPBYTESM1_Pos);  // 32 bytes
  cmd |= (0U << CRACENCORE_PK_COMMAND_FIELDF_Pos);  // GF(p)
  core_->PK.COMMAND = cmd;
  core_->PK.OPSIZE = 0x0100;
  return true;
}

Tampc::Tampc(uint32_t base)
    : tampc_(base == 0U ? nullptr : reinterpret_cast<NRF_TAMPC_Type*>(base)) {}

uint32_t Tampc::status() const {
  return (tampc_ == nullptr) ? 0U : tampc_->STATUS;
}

bool Tampc::tamperDetected() const {
  return (status() & (TAMPC_STATUS_ACTIVESHIELD_Msk | TAMPC_STATUS_PROTECT_Msk |
                      TAMPC_STATUS_CRACENTAMP_Msk |
                      TAMPC_STATUS_GLITCHSLOWDOMAIN0_Msk |
                      TAMPC_STATUS_GLITCHFASTDOMAIN0_Msk |
                      TAMPC_STATUS_GLITCHFASTDOMAIN1_Msk |
                      TAMPC_STATUS_GLITCHFASTDOMAIN2_Msk |
                      TAMPC_STATUS_GLITCHFASTDOMAIN3_Msk)) != 0U;
}

bool Tampc::writeErrorDetected() const {
  if (tampc_ == nullptr) {
    return false;
  }
  return ((tampc_->EVENTS_WRITEERROR &
           TAMPC_EVENTS_WRITEERROR_EVENTS_WRITEERROR_Msk) >>
          TAMPC_EVENTS_WRITEERROR_EVENTS_WRITEERROR_Pos) ==
         TAMPC_EVENTS_WRITEERROR_EVENTS_WRITEERROR_Generated;
}

bool Tampc::pollTamper(bool clearEvent) {
  if (tampc_ == nullptr) {
    return false;
  }
  const bool signaled =
      ((tampc_->EVENTS_TAMPER & TAMPC_EVENTS_TAMPER_EVENTS_TAMPER_Msk) >>
       TAMPC_EVENTS_TAMPER_EVENTS_TAMPER_Pos) ==
      TAMPC_EVENTS_TAMPER_EVENTS_TAMPER_Generated;
  if (signaled && clearEvent) {
    tampc_->EVENTS_TAMPER = 0U;
  }
  return signaled;
}

bool Tampc::pollWriteError(bool clearEvent) {
  if (tampc_ == nullptr) {
    return false;
  }
  const bool signaled =
      ((tampc_->EVENTS_WRITEERROR &
        TAMPC_EVENTS_WRITEERROR_EVENTS_WRITEERROR_Msk) >>
       TAMPC_EVENTS_WRITEERROR_EVENTS_WRITEERROR_Pos) ==
      TAMPC_EVENTS_WRITEERROR_EVENTS_WRITEERROR_Generated;
  if (signaled && clearEvent) {
    tampc_->EVENTS_WRITEERROR = 0U;
  }
  return signaled;
}

void Tampc::clearEvents() {
  if (tampc_ == nullptr) {
    return;
  }
  tampc_->EVENTS_TAMPER = 0U;
  tampc_->EVENTS_WRITEERROR = 0U;
}

void Tampc::enableInterrupts(bool tamper, bool writeError) {
  if (tampc_ == nullptr) {
    return;
  }
  uint32_t enableMask = 0U;
  uint32_t clearMask = 0U;
  if (tamper) {
    enableMask |= TAMPC_INTENSET_TAMPER_Msk;
  } else {
    clearMask |= TAMPC_INTENCLR_TAMPER_Msk;
  }
  if (writeError) {
    enableMask |= TAMPC_INTENSET_WRITEERROR_Msk;
  } else {
    clearMask |= TAMPC_INTENCLR_WRITEERROR_Msk;
  }
  if (enableMask != 0U) {
    tampc_->INTENSET = enableMask;
  }
  if (clearMask != 0U) {
    tampc_->INTENCLR = clearMask;
  }
}

bool Tampc::pendingTamperInterrupt() const {
  return (tampc_ != nullptr) &&
         ((tampc_->INTPEND & TAMPC_INTPEND_TAMPER_Msk) != 0U);
}

bool Tampc::pendingWriteErrorInterrupt() const {
  return (tampc_ != nullptr) &&
         ((tampc_->INTPEND & TAMPC_INTPEND_WRITEERROR_Msk) != 0U);
}

bool Tampc::writeProtectedControl(volatile uint32_t& ctrlRegister,
                                  uint32_t valueHigh,
                                  uint32_t valueLow,
                                  bool enable,
                                  bool lock) {
#if defined(NRF_TRUSTZONE_NONSECURE)
  (void)ctrlRegister;
  (void)valueHigh;
  (void)valueLow;
  (void)enable;
  (void)lock;
  return false;
#else
  if (tampc_ == nullptr) {
    return false;
  }
  ctrlRegister = kTampcWriteKey | kTampcWriteProtectionClear;
  ctrlRegister = kTampcWriteKey |
                 (enable ? valueHigh : valueLow) |
                 (lock ? kTampcLockEnabled : 0U);
  return !pollWriteError(false);
#endif
}

bool Tampc::setInternalResetOnTamper(bool enable, bool lock) {
  return writeProtectedControl(
      tampc_->PROTECT.INTRESETEN.CTRL,
      (TAMPC_PROTECT_INTRESETEN_CTRL_VALUE_High
       << TAMPC_PROTECT_INTRESETEN_CTRL_VALUE_Pos),
      (TAMPC_PROTECT_INTRESETEN_CTRL_VALUE_Low
       << TAMPC_PROTECT_INTRESETEN_CTRL_VALUE_Pos),
      enable, lock);
}

bool Tampc::setExternalResetOnTamper(bool enable, bool lock) {
  return writeProtectedControl(
      tampc_->PROTECT.EXTRESETEN.CTRL,
      (TAMPC_PROTECT_EXTRESETEN_CTRL_VALUE_High
       << TAMPC_PROTECT_EXTRESETEN_CTRL_VALUE_Pos),
      (TAMPC_PROTECT_EXTRESETEN_CTRL_VALUE_Low
       << TAMPC_PROTECT_EXTRESETEN_CTRL_VALUE_Pos),
      enable, lock);
}

bool Tampc::setEraseProtect(bool enable, bool lock) {
  return writeProtectedControl(
      tampc_->PROTECT.ERASEPROTECT.CTRL,
      (TAMPC_PROTECT_ERASEPROTECT_CTRL_VALUE_High
       << TAMPC_PROTECT_ERASEPROTECT_CTRL_VALUE_Pos),
      (TAMPC_PROTECT_ERASEPROTECT_CTRL_VALUE_Low
       << TAMPC_PROTECT_ERASEPROTECT_CTRL_VALUE_Pos),
      enable, lock);
}

bool Tampc::setCracenTamperMonitor(bool enable, bool lock) {
  return writeProtectedControl(
      tampc_->PROTECT.CRACENTAMP.CTRL,
      (TAMPC_PROTECT_CRACENTAMP_CTRL_VALUE_High
       << TAMPC_PROTECT_CRACENTAMP_CTRL_VALUE_Pos),
      (TAMPC_PROTECT_CRACENTAMP_CTRL_VALUE_Low
       << TAMPC_PROTECT_CRACENTAMP_CTRL_VALUE_Pos),
      enable, lock);
}

bool Tampc::setActiveShieldMonitor(bool enable, bool lock) {
  return writeProtectedControl(
      tampc_->PROTECT.ACTIVESHIELD.CTRL,
      (TAMPC_PROTECT_ACTIVESHIELD_CTRL_VALUE_High
       << TAMPC_PROTECT_ACTIVESHIELD_CTRL_VALUE_Pos),
      (TAMPC_PROTECT_ACTIVESHIELD_CTRL_VALUE_Low
       << TAMPC_PROTECT_ACTIVESHIELD_CTRL_VALUE_Pos),
      enable, lock);
}

bool Tampc::setGlitchSlowMonitor(bool enable, bool lock) {
  return writeProtectedControl(
      tampc_->PROTECT.GLITCHSLOWDOMAIN.CTRL,
      (TAMPC_PROTECT_GLITCHSLOWDOMAIN_CTRL_VALUE_High
       << TAMPC_PROTECT_GLITCHSLOWDOMAIN_CTRL_VALUE_Pos),
      (TAMPC_PROTECT_GLITCHSLOWDOMAIN_CTRL_VALUE_Low
       << TAMPC_PROTECT_GLITCHSLOWDOMAIN_CTRL_VALUE_Pos),
      enable, lock);
}

bool Tampc::setGlitchFastMonitor(bool enable, bool lock) {
  return writeProtectedControl(
      tampc_->PROTECT.GLITCHFASTDOMAIN.CTRL,
      (TAMPC_PROTECT_GLITCHFASTDOMAIN_CTRL_VALUE_High
       << TAMPC_PROTECT_GLITCHFASTDOMAIN_CTRL_VALUE_Pos),
      (TAMPC_PROTECT_GLITCHFASTDOMAIN_CTRL_VALUE_Low
       << TAMPC_PROTECT_GLITCHFASTDOMAIN_CTRL_VALUE_Pos),
      enable, lock);
}

bool Tampc::setActiveShieldChannelMask(uint8_t channelMask) {
#if defined(NRF_TRUSTZONE_NONSECURE)
  (void)channelMask;
  return false;
#else
  if (tampc_ == nullptr) {
    return false;
  }
  tampc_->ACTIVESHIELD.CHEN = static_cast<uint32_t>(channelMask & 0x0FU);
  return true;
#endif
}

uint8_t Tampc::activeShieldChannelMask() const {
  if (tampc_ == nullptr) {
    return 0U;
  }
  return static_cast<uint8_t>(tampc_->ACTIVESHIELD.CHEN & 0x0FU);
}

bool Tampc::activeShieldChannelEnabled(uint8_t channel) const {
  if (channel > 3U) {
    return false;
  }
  return (activeShieldChannelMask() & static_cast<uint8_t>(1U << channel)) != 0U;
}

bool Tampc::setActiveShieldChannelEnabled(uint8_t channel, bool enable) {
  if (channel > 3U) {
    return false;
  }
  uint8_t mask = activeShieldChannelMask();
  if (enable) {
    mask = static_cast<uint8_t>(mask | static_cast<uint8_t>(1U << channel));
  } else {
    mask = static_cast<uint8_t>(mask & ~static_cast<uint8_t>(1U << channel));
  }
  return setActiveShieldChannelMask(mask);
}

bool Tampc::internalResetOnTamperEnabled() const {
  return protectedSignalEnabled(tampc_->PROTECT.INTRESETEN.CTRL);
}

bool Tampc::externalResetOnTamperEnabled() const {
  return protectedSignalEnabled(tampc_->PROTECT.EXTRESETEN.CTRL);
}

bool Tampc::eraseProtectEnabled() const {
  return protectedSignalEnabled(tampc_->PROTECT.ERASEPROTECT.CTRL);
}

bool Tampc::cracenTamperMonitorEnabled() const {
  return protectedSignalEnabled(tampc_->PROTECT.CRACENTAMP.CTRL);
}

bool Tampc::activeShieldMonitorEnabled() const {
  return protectedSignalEnabled(tampc_->PROTECT.ACTIVESHIELD.CTRL);
}

bool Tampc::glitchSlowMonitorEnabled() const {
  return protectedSignalEnabled(tampc_->PROTECT.GLITCHSLOWDOMAIN.CTRL);
}

bool Tampc::glitchFastMonitorEnabled() const {
  return protectedSignalEnabled(tampc_->PROTECT.GLITCHFASTDOMAIN.CTRL);
}

bool Tampc::domainDbgenEnabled(uint8_t domain) const {
  return validDomainIndex(domain) &&
         protectedSignalEnabled(tampc_->PROTECT.DOMAIN[domain].DBGEN.CTRL);
}

bool Tampc::domainNidenEnabled(uint8_t domain) const {
  return validDomainIndex(domain) &&
         protectedSignalEnabled(tampc_->PROTECT.DOMAIN[domain].NIDEN.CTRL);
}

bool Tampc::domainSpidenEnabled(uint8_t domain) const {
  return validDomainIndex(domain) &&
         protectedSignalEnabled(tampc_->PROTECT.DOMAIN[domain].SPIDEN.CTRL);
}

bool Tampc::domainSpnidenEnabled(uint8_t domain) const {
  return validDomainIndex(domain) &&
         protectedSignalEnabled(tampc_->PROTECT.DOMAIN[domain].SPNIDEN.CTRL);
}

bool Tampc::apDbgenEnabled(uint8_t ap) const {
  return validApIndex(ap) &&
         protectedSignalEnabled(tampc_->PROTECT.AP[ap].DBGEN.CTRL);
}

bool Tampc::setDomainDbgen(bool enable, bool lock, uint8_t domain) {
  return validDomainIndex(domain) &&
         writeProtectedControl(
             tampc_->PROTECT.DOMAIN[domain].DBGEN.CTRL,
             (TAMPC_PROTECT_DOMAIN_DBGEN_CTRL_VALUE_High
              << TAMPC_PROTECT_DOMAIN_DBGEN_CTRL_VALUE_Pos),
             (TAMPC_PROTECT_DOMAIN_DBGEN_CTRL_VALUE_Low
              << TAMPC_PROTECT_DOMAIN_DBGEN_CTRL_VALUE_Pos),
             enable, lock);
}

bool Tampc::setDomainNiden(bool enable, bool lock, uint8_t domain) {
  return validDomainIndex(domain) &&
         writeProtectedControl(
             tampc_->PROTECT.DOMAIN[domain].NIDEN.CTRL,
             (TAMPC_PROTECT_DOMAIN_NIDEN_CTRL_VALUE_High
              << TAMPC_PROTECT_DOMAIN_NIDEN_CTRL_VALUE_Pos),
             (TAMPC_PROTECT_DOMAIN_NIDEN_CTRL_VALUE_Low
              << TAMPC_PROTECT_DOMAIN_NIDEN_CTRL_VALUE_Pos),
             enable, lock);
}

bool Tampc::setDomainSpiden(bool enable, bool lock, uint8_t domain) {
  return validDomainIndex(domain) &&
         writeProtectedControl(
             tampc_->PROTECT.DOMAIN[domain].SPIDEN.CTRL,
             (TAMPC_PROTECT_DOMAIN_SPIDEN_CTRL_VALUE_High
              << TAMPC_PROTECT_DOMAIN_SPIDEN_CTRL_VALUE_Pos),
             (TAMPC_PROTECT_DOMAIN_SPIDEN_CTRL_VALUE_Low
              << TAMPC_PROTECT_DOMAIN_SPIDEN_CTRL_VALUE_Pos),
             enable, lock);
}

bool Tampc::setDomainSpniden(bool enable, bool lock, uint8_t domain) {
  return validDomainIndex(domain) &&
         writeProtectedControl(
             tampc_->PROTECT.DOMAIN[domain].SPNIDEN.CTRL,
             (TAMPC_PROTECT_DOMAIN_SPNIDEN_CTRL_VALUE_High
              << TAMPC_PROTECT_DOMAIN_SPNIDEN_CTRL_VALUE_Pos),
             (TAMPC_PROTECT_DOMAIN_SPNIDEN_CTRL_VALUE_Low
              << TAMPC_PROTECT_DOMAIN_SPNIDEN_CTRL_VALUE_Pos),
             enable, lock);
}

bool Tampc::setApDbgen(bool enable, bool lock, uint8_t ap) {
  return validApIndex(ap) &&
         writeProtectedControl(
             tampc_->PROTECT.AP[ap].DBGEN.CTRL,
             (TAMPC_PROTECT_AP_DBGEN_CTRL_VALUE_High
              << TAMPC_PROTECT_AP_DBGEN_CTRL_VALUE_Pos),
             (TAMPC_PROTECT_AP_DBGEN_CTRL_VALUE_Low
              << TAMPC_PROTECT_AP_DBGEN_CTRL_VALUE_Pos),
             enable, lock);
}

bool Tampc::protectStatusError() const {
  return tamperDetected() || writeErrorDetected();
}

bool Tampc::cracenTamperStatusError() const {
  return protectedStatusError(tampc_->PROTECT.CRACENTAMP.STATUS);
}

bool Tampc::activeShieldStatusError() const {
  return protectedStatusError(tampc_->PROTECT.ACTIVESHIELD.STATUS);
}

bool Tampc::glitchSlowStatusError() const {
  return protectedStatusError(tampc_->PROTECT.GLITCHSLOWDOMAIN.STATUS);
}

bool Tampc::glitchFastStatusError() const {
  return protectedStatusError(tampc_->PROTECT.GLITCHFASTDOMAIN.STATUS);
}

bool Tampc::intResetStatusError() const {
  return protectedStatusError(tampc_->PROTECT.INTRESETEN.STATUS);
}

bool Tampc::extResetStatusError() const {
  return protectedStatusError(tampc_->PROTECT.EXTRESETEN.STATUS);
}

bool Tampc::eraseProtectStatusError() const {
  return protectedStatusError(tampc_->PROTECT.ERASEPROTECT.STATUS);
}

bool Tampc::domainDbgenStatusError(uint8_t domain) const {
  return validDomainIndex(domain) &&
         protectedStatusError(tampc_->PROTECT.DOMAIN[domain].DBGEN.STATUS);
}

bool Tampc::domainNidenStatusError(uint8_t domain) const {
  return validDomainIndex(domain) &&
         protectedStatusError(tampc_->PROTECT.DOMAIN[domain].NIDEN.STATUS);
}

bool Tampc::domainSpidenStatusError(uint8_t domain) const {
  return validDomainIndex(domain) &&
         protectedStatusError(tampc_->PROTECT.DOMAIN[domain].SPIDEN.STATUS);
}

bool Tampc::domainSpnidenStatusError(uint8_t domain) const {
  return validDomainIndex(domain) &&
         protectedStatusError(tampc_->PROTECT.DOMAIN[domain].SPNIDEN.STATUS);
}

bool Tampc::apDbgenStatusError(uint8_t ap) const {
  return validApIndex(ap) &&
         protectedStatusError(tampc_->PROTECT.AP[ap].DBGEN.STATUS);
}

bool Tampc::protectedSignalEnabled(const volatile uint32_t& ctrlRegister) const {
  return (static_cast<uint32_t>(ctrlRegister) & 0x1U) != 0U;
}

bool Tampc::protectedStatusError(const volatile uint32_t& statusRegister) const {
  return (static_cast<uint32_t>(statusRegister) & 0x1U) != 0U;
}

bool Tampc::validDomainIndex(uint8_t domain) const {
  return (tampc_ != nullptr) && (domain < TAMPC_PROTECT_DOMAIN_MaxCount);
}

bool Tampc::validApIndex(uint8_t ap) const {
  return (tampc_ != nullptr) && (ap < TAMPC_PROTECT_AP_MaxCount);
}

}  // namespace xiao_nrf54l15
