
#include <nrf54_all.h>
#include "openthread_platform_nrf54l15.h"

#if !defined(NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE) || \
    (NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE == 0)
#error "Enable Tools > Thread Core > Experimental Stage Core before building this example."
#endif

namespace {

constexpr char kJoinerPskd[] = "THREAD54";
constexpr uint32_t kStatusPrintIntervalMs = 3000U;
constexpr uint32_t kJoinerStartRetryMs = 2000U;

xiao_nrf54l15::Nrf54ThreadExperimental g_thread;
uint32_t g_lastStatusPrintMs = 0U;
uint32_t g_lastJoinerStartAttemptMs = 0U;
bool g_printedRole = false;
xiao_nrf54l15::Nrf54ThreadExperimental::Role g_lastRole =
    xiao_nrf54l15::Nrf54ThreadExperimental::Role::kUnknown;
bool g_joinerStarted = false;
bool g_joinerComplete = false;
bool g_cleanDatasetChecked = false;
bool g_preexistingDataset = false;
bool g_blockedByPreexistingDataset = false;

bool readActiveDatasetLength(uint16_t* outLength) {
  if (outLength != nullptr) {
    *outLength = 0U;
  }

  otOperationalDatasetTlvs datasetTlvs = {};
  const bool haveDataset = g_thread.getActiveDatasetTlvs(&datasetTlvs);
  if (haveDataset && outLength != nullptr) {
    *outLength = datasetTlvs.mLength;
  }
  return haveDataset;
}

void printHexBytes(const uint8_t* data, uint16_t length) {
  static const char kHex[] = "0123456789ABCDEF";
  for (uint16_t i = 0; i < length; ++i) {
    const uint8_t value = data[i];
    Serial.print(kHex[(value >> 4U) & 0x0FU]);
    Serial.print(kHex[value & 0x0FU]);
  }
}

void printActiveDatasetStatus(const char* label) {
  otOperationalDatasetTlvs datasetTlvs = {};
  const bool haveDataset = g_thread.getActiveDatasetTlvs(&datasetTlvs);
  Serial.print("thread_joiner ");
  Serial.print(label);
  Serial.print("_active_dataset=");
  Serial.println(haveDataset ? 1 : 0);
  Serial.print("thread_joiner ");
  Serial.print(label);
  Serial.print("_active_dataset_tlv_len=");
  Serial.println(haveDataset ? datasetTlvs.mLength : 0U);
  if (haveDataset) {
    Serial.print("thread_joiner ");
    Serial.print(label);
    Serial.print("_active_dataset_tlv_hex=");
    printHexBytes(datasetTlvs.mTlvs, datasetTlvs.mLength);
    Serial.println();
  }
}

void printPlatformDebug() {
  xiao_nrf54l15::OpenThreadPlatformSkeletonSnapshot snapshot = {};
  if (!xiao_nrf54l15::OpenThreadPlatformSkeleton::snapshot(&snapshot)) {
    return;
  }

  Serial.print("thread_joiner pal process=");
  Serial.print(snapshot.processCount);
  Serial.print(" alarm_ms=");
  Serial.print(snapshot.alarmMilliFires);
  Serial.print(" tx_req=");
  Serial.print(snapshot.txRequestCount);
  Serial.print(" tx_done=");
  Serial.print(snapshot.radioTxDoneCount);
  Serial.print(" rx_done=");
  Serial.print(snapshot.radioRxDoneCount);
  Serial.print(" rx_poll=");
  Serial.print(snapshot.radioReceivePollCount);
  Serial.print(" filtered=");
  Serial.print(snapshot.radioFilteredCount);
  Serial.print(" crc=");
  Serial.print(snapshot.radioRxCrcErrorCount);
  Serial.print(" invalid=");
  Serial.print(snapshot.radioRxInvalidLengthCount);
  Serial.print(" rxat_sched=");
  Serial.print(snapshot.radioReceiveAtScheduleCount);
  Serial.print(" rxat_start=");
  Serial.print(snapshot.radioReceiveAtStartCount);
  Serial.print(" rxat_timeout=");
  Serial.print(snapshot.radioReceiveAtTimeoutCount);
  Serial.print(" rxat_late=");
  Serial.print(snapshot.radioReceiveAtLateCount);
  Serial.print(" radio_state=");
  Serial.print(static_cast<int>(snapshot.radioState));
  Serial.print(" channel=");
  Serial.print(snapshot.radioChannel);
  Serial.print(" pan=0x");
  Serial.print(snapshot.panId, HEX);
  Serial.print(" short=0x");
  Serial.print(snapshot.shortAddress, HEX);
  Serial.print(" rx_idle=");
  Serial.print(snapshot.radioRxOnWhenIdle ? 1 : 0);
  Serial.print(" last_err=");
  Serial.print(snapshot.radioLastError);
  Serial.print(" last_tx_len=");
  Serial.print(snapshot.radioLastTxLength);
  Serial.print(" last_rx_len=");
  Serial.print(snapshot.radioLastRxLength);
  Serial.print(" last_rx_dst=0x");
  Serial.print(snapshot.radioLastRxDestinationShort, HEX);
  Serial.print(" last_tx_seq=");
  Serial.print(snapshot.radioLastTxSequence);
  Serial.print(" last_rx_seq=");
  Serial.print(snapshot.radioLastRxSequence);
  Serial.print(" last_tx_type=");
  Serial.print(snapshot.radioLastTxFrameType);
  Serial.print(" last_rx_type=");
  Serial.print(snapshot.radioLastRxFrameType);
  Serial.print(" last_log=");
  Serial.println(snapshot.lastLogLine);

  for (size_t i = 0; i < 4U; ++i) {
    if (snapshot.recentLogLines[i][0] == '\0') {
      continue;
    }
    Serial.print("thread_joiner otlog[");
    Serial.print(i);
    Serial.print("]=");
    Serial.println(snapshot.recentLogLines[i]);
  }

  for (size_t i = 0; i < 4U; ++i) {
    if (snapshot.recentMleLogLines[i][0] == '\0') {
      continue;
    }
    Serial.print("thread_joiner mlelog[");
    Serial.print(i);
    Serial.print("]=");
    Serial.println(snapshot.recentMleLogLines[i]);
  }
}

void printAttachDebug() {
  xiao_nrf54l15::Nrf54ThreadExperimental::AttachDebugState attach = {};
  if (!g_thread.getAttachDebugState(&attach)) {
    return;
  }

  Serial.print("thread_joiner attach_debug valid=");
  Serial.print(attach.valid ? 1 : 0);
  Serial.print(" in_progress=");
  Serial.print(attach.attachInProgress ? 1 : 0);
  Serial.print(" timer=");
  Serial.print(attach.attachTimerRunning ? 1 : 0);
  Serial.print(" got_parent_response=");
  Serial.print(attach.receivedResponseFromParent ? 1 : 0);
  Serial.print(" state=");
  Serial.print(attach.attachStateName);
  Serial.print(" mode=");
  Serial.print(attach.attachModeName);
  Serial.print(" reattach=");
  Serial.print(attach.reattachModeName);
  Serial.print(" parent_req=");
  Serial.print(attach.parentRequestCounter);
  Serial.print(" child_id_left=");
  Serial.print(attach.childIdRequestsRemaining);
  Serial.print(" attach_count=");
  Serial.print(attach.attachCounter);
  Serial.print(" parent_state=");
  Serial.print(attach.parentCandidateStateName);
  Serial.print(" parent_rloc16=0x");
  Serial.print(attach.parentCandidateRloc16, HEX);
  Serial.print(" timer_left_ms=");
  Serial.println(attach.attachTimerRemainingMs);
}

void onJoinerCallback(void* context, otError error) {
  Serial.print("thread_joiner joiner_callback error=");
  Serial.println(static_cast<int>(error));
  if (error == OT_ERROR_NONE) {
    g_joinerComplete = true;
    Serial.println("thread_joiner meshcop_joiner_callback_success=1");
    printActiveDatasetStatus("after_joiner_callback");
    Serial.println("thread_joiner JOIN_SUCCESS - device is now on the Thread network");
    Serial.print("thread_joiner role=");
    Serial.println(g_thread.roleName());
  } else {
    g_joinerStarted = false;
    Serial.println("thread_joiner JOIN_FAILED - check PSKd and commissioner");
  }
}

void onStateChanged(void* context, otChangedFlags flags,
                    xiao_nrf54l15::Nrf54ThreadExperimental::Role role) {
  Serial.print("thread_joiner state_changed flags=0x");
  Serial.print(static_cast<unsigned long>(flags), HEX);
  Serial.print(" role=");
  Serial.println(xiao_nrf54l15::Nrf54ThreadExperimental::roleName(role));
}

void printStatus(const char* reason) {
  Serial.print("thread_joiner reason=");
  Serial.println(reason);
  Serial.print("thread_joiner started=");
  Serial.println(g_thread.started() ? 1 : 0);
  Serial.print("thread_joiner attached=");
  Serial.println(g_thread.attached() ? 1 : 0);
  Serial.print("thread_joiner role=");
  Serial.println(g_thread.roleName());
  Serial.print("thread_joiner rloc16=0x");
  Serial.println(g_thread.rloc16(), HEX);
  Serial.print("thread_joiner joiner_supported=");
  Serial.println(g_thread.joinerSupported() ? 1 : 0);
  Serial.print("thread_joiner joiner_active=");
  Serial.println(g_thread.joinerActive() ? 1 : 0);
  Serial.print("thread_joiner joiner_state=");
  Serial.println(g_thread.joinerStateName());
  Serial.print("thread_joiner joiner_complete=");
  Serial.println(g_joinerComplete ? 1 : 0);
  Serial.print("thread_joiner clean_dataset_checked=");
  Serial.println(g_cleanDatasetChecked ? 1 : 0);
  Serial.print("thread_joiner preexisting_dataset_before_joiner=");
  Serial.println(g_preexistingDataset ? 1 : 0);
  printActiveDatasetStatus("status");
  printAttachDebug();
  printPlatformDebug();
}

}  // namespace

void setup() {
  Serial.begin(115200);
  const uint32_t waitStart = millis();
  while (!Serial && (millis() - waitStart) < 1500UL) {
  }

  Serial.println("thread_joiner Joiner example starting...");
  Serial.print("thread_joiner PSKd=");
  Serial.println(kJoinerPskd);
  Serial.println("thread_joiner wipe_requested=1");
  Serial.print("thread_joiner standard_meshcop_enabled=");
  Serial.println(g_thread.joinerSupported() ? 1 : 0);

  const bool beginOk = g_thread.beginJoinerOnly(true);
  Serial.print("thread_joiner begin=");
  Serial.println(beginOk ? 1 : 0);
  if (!beginOk) {
    Serial.println("thread_joiner FATAL begin failed");
    return;
  }

  g_thread.setStateChangedCallback(onStateChanged, nullptr);
  printStatus("boot");

  if (!g_thread.joinerSupported()) {
    Serial.println(
        "thread_joiner Standard MeshCoP Joiner is not compiled in this staged "
        "core yet.");
    Serial.println(
        "thread_joiner Use the PSK UDP examples for current two-board joining "
        "tests.");
    return;
  }

  Serial.println("thread_joiner Waiting for OpenThread joiner interface...");
}

void loop() {
  g_thread.process();

  if (g_blockedByPreexistingDataset) {
    if ((millis() - g_lastStatusPrintMs) >= kStatusPrintIntervalMs) {
      g_lastStatusPrintMs = millis();
      printStatus("blocked-preexisting-dataset");
    }
    return;
  }

  if (!g_joinerStarted && !g_joinerComplete &&
      g_thread.rawInstance() != nullptr &&
      (millis() - g_lastJoinerStartAttemptMs) >= kJoinerStartRetryMs) {
    g_lastJoinerStartAttemptMs = millis();

    if (!g_cleanDatasetChecked) {
      uint16_t datasetLength = 0U;
      g_preexistingDataset = readActiveDatasetLength(&datasetLength);
      g_cleanDatasetChecked = true;
      Serial.print("thread_joiner before_joiner_active_dataset=");
      Serial.println(g_preexistingDataset ? 1 : 0);
      Serial.print("thread_joiner before_joiner_active_dataset_tlv_len=");
      Serial.println(datasetLength);
      if (g_preexistingDataset) {
        g_blockedByPreexistingDataset = true;
        Serial.println(
            "thread_joiner FATAL preexisting_dataset_before_joiner=1");
        return;
      }
      Serial.println("thread_joiner clean_joiner_proof=1");
    }

    const bool joinerOk = g_thread.startJoiner(kJoinerPskd, nullptr,
                                               onJoinerCallback, nullptr);
    Serial.print("thread_joiner joiner_start=");
    Serial.println(joinerOk ? 1 : 0);
    if (!joinerOk) {
      Serial.print("thread_joiner joiner_error=");
      Serial.println(static_cast<int>(g_thread.lastError()));
    } else {
      g_joinerStarted = true;
      Serial.println("thread_joiner Waiting for commissioner to accept this joiner...");
    }
  }

  const xiao_nrf54l15::Nrf54ThreadExperimental::Role currentRole =
      g_thread.role();
  if (currentRole != g_lastRole) {
    g_lastRole = currentRole;
    printStatus("role-change");
  }

  if ((millis() - g_lastStatusPrintMs) >= kStatusPrintIntervalMs) {
    g_lastStatusPrintMs = millis();
    printStatus("heartbeat");
  }
}
