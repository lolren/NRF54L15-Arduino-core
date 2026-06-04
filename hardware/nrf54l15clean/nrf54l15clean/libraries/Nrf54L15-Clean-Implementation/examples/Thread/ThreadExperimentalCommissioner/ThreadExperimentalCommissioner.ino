
#include <nrf54_all.h>
#include "openthread_platform_nrf54l15.h"

#if !defined(NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE) || \
    (NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE == 0)
#error "Enable Tools > Thread Core > Experimental Stage Core before building this example."
#endif

namespace {

constexpr char kCommissionerPskd[] = "THREAD54";
constexpr uint32_t kStatusPrintIntervalMs = 3000U;
constexpr uint32_t kJoinerAddRetryMs = 2000U;
constexpr uint32_t kJoinerRefreshMs = 60000U;
constexpr uint32_t kJoinerEntryTimeoutSeconds = 300U;

xiao_nrf54l15::Nrf54ThreadExperimental g_thread;
uint32_t g_lastStatusPrintMs = 0U;
uint32_t g_lastJoinerAddAttemptMs = 0U;
xiao_nrf54l15::Nrf54ThreadExperimental::Role g_lastRole =
    xiao_nrf54l15::Nrf54ThreadExperimental::Role::kUnknown;
bool g_commissionerStarted = false;
bool g_joinerEntryAdded = false;
bool g_joinerConnected = false;
uint32_t g_meshcopFinalizeEvents = 0U;

void printJoinerId(const char* prefix, const otExtAddress* joinerId) {
  if (joinerId == nullptr) {
    Serial.print(prefix);
    Serial.println("null");
    return;
  }

  char eui64Hex[OT_EXT_ADDRESS_SIZE * 2 + 1] = {0};
  for (size_t i = 0; i < OT_EXT_ADDRESS_SIZE; ++i) {
    snprintf(&eui64Hex[i * 2], 3, "%02X",
             static_cast<unsigned>(joinerId->m8[i]));
  }
  Serial.print(prefix);
  Serial.println(eui64Hex);
}

void printConfiguredDatasetStatus(const char* label) {
  otOperationalDatasetTlvs datasetTlvs = {};
  const bool haveDataset = g_thread.getConfiguredOrActiveDatasetTlvs(&datasetTlvs);
  Serial.print("thread_commissioner ");
  Serial.print(label);
  Serial.print("_dataset=");
  Serial.println(haveDataset ? 1 : 0);
  Serial.print("thread_commissioner ");
  Serial.print(label);
  Serial.print("_dataset_tlv_len=");
  Serial.println(haveDataset ? datasetTlvs.mLength : 0);
}

void printActiveDatasetStatus(const char* label) {
  otOperationalDatasetTlvs datasetTlvs = {};
  const bool haveDataset = g_thread.getActiveDatasetTlvs(&datasetTlvs);
  Serial.print("thread_commissioner ");
  Serial.print(label);
  Serial.print("_active_dataset=");
  Serial.println(haveDataset ? 1 : 0);
  Serial.print("thread_commissioner ");
  Serial.print(label);
  Serial.print("_active_dataset_tlv_len=");
  Serial.println(haveDataset ? datasetTlvs.mLength : 0);
}

void printPlatformDebug() {
  xiao_nrf54l15::OpenThreadPlatformSkeletonSnapshot snapshot = {};
  if (!xiao_nrf54l15::OpenThreadPlatformSkeleton::snapshot(&snapshot)) {
    return;
  }

  Serial.print("thread_commissioner pal process=");
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
    Serial.print("thread_commissioner otlog[");
    Serial.print(i);
    Serial.print("]=");
    Serial.println(snapshot.recentLogLines[i]);
  }

  for (size_t i = 0; i < 4U; ++i) {
    if (snapshot.recentMleLogLines[i][0] == '\0') {
      continue;
    }
    Serial.print("thread_commissioner mlelog[");
    Serial.print(i);
    Serial.print("]=");
    Serial.println(snapshot.recentMleLogLines[i]);
  }
}

void tryAddJoinerEntry(const char* reason, bool refresh = false) {
  if ((!refresh && g_joinerEntryAdded) || !g_thread.commissionerActive()) {
    return;
  }

  const bool added =
      g_thread.addJoinerToCommissioner(kCommissionerPskd,
                                       kJoinerEntryTimeoutSeconds);
  Serial.print("thread_commissioner joiner_added=");
  Serial.println(added ? 1 : 0);
  Serial.print("thread_commissioner joiner_add_reason=");
  Serial.println(reason);
  if (!added) {
    Serial.print("thread_commissioner joiner_add_error=");
    Serial.println(static_cast<int>(g_thread.lastError()));
    g_joinerEntryAdded = false;
    return;
  }

  g_joinerEntryAdded = true;
  Serial.print("thread_commissioner pskd=");
  Serial.println(kCommissionerPskd);
  Serial.println("thread_commissioner Waiting for joiner with this PSKd...");
}

void onCommissionerJoinerCallback(void* context,
                                  const otExtAddress* joinerId,
                                  otError error) {
  printJoinerId("thread_commissioner joiner_event eui64=", joinerId);

  Serial.print("thread_commissioner joiner_event error=");
  Serial.println(static_cast<int>(error));
  if (error == OT_ERROR_NONE) {
    g_joinerConnected = true;
    Serial.println("thread_commissioner meshcop_joiner_finalize_callback=1");
    Serial.println("thread_commissioner JOINER_ACCEPTED - device was commissioned");
    printActiveDatasetStatus("after_joiner_finalize");
  } else {
    Serial.println("thread_commissioner meshcop_joiner_removed_callback=1");
  }
}

void onCommissionerJoinerEventCallback(void* context,
                                       otCommissionerJoinerEvent event,
                                       const otJoinerInfo* joinerInfo,
                                       const otExtAddress* joinerId) {
  (void)context;
  (void)joinerInfo;

  Serial.print("thread_commissioner meshcop_joiner_event=");
  Serial.print(static_cast<int>(event));
  Serial.print(" name=");
  Serial.println(
      xiao_nrf54l15::Nrf54ThreadExperimental::commissionerJoinerEventName(
          event));
  printJoinerId("thread_commissioner meshcop_joiner_event_eui64=", joinerId);
  Serial.print("thread_commissioner meshcop_joiner_event_count=");
  Serial.println(g_thread.commissionerJoinerEventCount());
  Serial.print("thread_commissioner meshcop_joiner_finalize_count=");
  Serial.println(g_thread.commissionerJoinerFinalizeCount());
  Serial.print("thread_commissioner meshcop_joiner_removed_count=");
  Serial.println(g_thread.commissionerJoinerRemovedCount());

  if (event == OT_COMMISSIONER_JOINER_FINALIZE) {
    ++g_meshcopFinalizeEvents;
    Serial.println("thread_commissioner meshcop_joiner_finalize=1");
  }
}

void onCommissionerStateCallback(void* context,
                                 otCommissionerState state) {
  Serial.print("thread_commissioner state_changed state=");
  Serial.println(g_thread.commissionerStateName());
  if (state == OT_COMMISSIONER_STATE_ACTIVE) {
    g_commissionerStarted = true;
    tryAddJoinerEntry("commissioner-active");
  } else if (state == OT_COMMISSIONER_STATE_DISABLED) {
    g_commissionerStarted = false;
    g_joinerEntryAdded = false;
  }
}

void onStateChanged(void* context, otChangedFlags flags,
                    xiao_nrf54l15::Nrf54ThreadExperimental::Role role) {
  Serial.print("thread_commissioner thread_state flags=0x");
  Serial.print(static_cast<unsigned long>(flags), HEX);
  Serial.print(" role=");
  Serial.println(xiao_nrf54l15::Nrf54ThreadExperimental::roleName(role));

  if (role == xiao_nrf54l15::Nrf54ThreadExperimental::Role::kLeader &&
      !g_commissionerStarted) {
    if (!g_thread.commissionerSupported()) {
      g_commissionerStarted = true;
      Serial.println(
          "thread_commissioner Standard MeshCoP Commissioner is not compiled "
          "in this staged core yet.");
      Serial.println(
          "thread_commissioner Use the PSK UDP examples for current two-board "
          "joining tests.");
      return;
    }

    Serial.println("thread_commissioner LEADER detected — starting commissioner...");
    const bool ok = g_thread.startCommissioner();
    Serial.print("thread_commissioner commissioner_start=");
    Serial.println(ok ? 1 : 0);
    if (ok) {
      g_commissionerStarted = true;
    }
  }
}

void printStatus(const char* reason) {
  Serial.print("thread_commissioner reason=");
  Serial.println(reason);
  Serial.print("thread_commissioner started=");
  Serial.println(g_thread.started() ? 1 : 0);
  Serial.print("thread_commissioner attached=");
  Serial.println(g_thread.attached() ? 1 : 0);
  Serial.print("thread_commissioner role=");
  Serial.println(g_thread.roleName());
  Serial.print("thread_commissioner rloc16=0x");
  Serial.println(g_thread.rloc16(), HEX);
  Serial.print("thread_commissioner commissioner_supported=");
  Serial.println(g_thread.commissionerSupported() ? 1 : 0);
  Serial.print("thread_commissioner commissioner_active=");
  Serial.println(g_thread.commissionerActive() ? 1 : 0);
  Serial.print("thread_commissioner commissioner_state=");
  Serial.println(g_thread.commissionerStateName());
  if (g_thread.commissionerActive()) {
    uint16_t sessionId = 0U;
    if (g_thread.commissionerSessionId(&sessionId)) {
      Serial.print("thread_commissioner commissioner_session_id=");
      Serial.println(sessionId);
    }
  }
  Serial.print("thread_commissioner meshcop_last_event=");
  Serial.println(g_thread.lastCommissionerJoinerEventName());
  Serial.print("thread_commissioner meshcop_event_count=");
  Serial.println(g_thread.commissionerJoinerEventCount());
  Serial.print("thread_commissioner meshcop_finalize_count=");
  Serial.println(g_thread.commissionerJoinerFinalizeCount());
  Serial.print("thread_commissioner meshcop_removed_count=");
  Serial.println(g_thread.commissionerJoinerRemovedCount());
  Serial.print("thread_commissioner meshcop_finalize_seen=");
  Serial.println(g_meshcopFinalizeEvents);
  Serial.print("thread_commissioner joiner_connected=");
  Serial.println(g_joinerConnected ? 1 : 0);
  Serial.print("thread_commissioner joiner_entry_added=");
  Serial.println(g_joinerEntryAdded ? 1 : 0);
  printConfiguredDatasetStatus("configured_or_active");
  printActiveDatasetStatus("status");
  printPlatformDebug();
}

}  // namespace

void setup() {
  Serial.begin(115200);
  const uint32_t waitStart = millis();
  while (!Serial && (millis() - waitStart) < 1500UL) {
  }

  Serial.println("thread_commissioner Commissioner example starting...");
  Serial.print("thread_commissioner PSKd=");
  Serial.println(kCommissionerPskd);
  Serial.println("thread_commissioner wipe_requested=1");
  Serial.print("thread_commissioner standard_meshcop_enabled=");
  Serial.println(g_thread.commissionerSupported() ? 1 : 0);

  const bool beginOk = g_thread.beginAsRouter(true);
  Serial.print("thread_commissioner begin=");
  Serial.println(beginOk ? 1 : 0);
  if (!beginOk) {
    Serial.println("thread_commissioner FATAL begin failed");
    return;
  }

  g_thread.setStateChangedCallback(onStateChanged, nullptr);
  g_thread.setCommissionerStateCallback(onCommissionerStateCallback, nullptr);
  g_thread.setCommissionerJoinerCallback(onCommissionerJoinerCallback, nullptr);
  g_thread.setCommissionerJoinerEventCallback(onCommissionerJoinerEventCallback,
                                              nullptr);

  otOperationalDataset dataset = {};
  xiao_nrf54l15::Nrf54ThreadExperimental::buildDemoDataset(&dataset);
  printConfiguredDatasetStatus("before_set");
  const bool setOk = g_thread.setActiveDataset(dataset);
  Serial.print("thread_commissioner dataset_set=");
  Serial.println(setOk ? 1 : 0);
  printConfiguredDatasetStatus("after_set");

  printStatus("boot");
  Serial.println("thread_commissioner Will become Leader, then start Commissioner...");
}

void loop() {
  g_thread.process();

  if (g_thread.commissionerActive()) {
    const uint32_t nowMs = millis();
    const bool refresh =
        g_joinerEntryAdded &&
        ((nowMs - g_lastJoinerAddAttemptMs) >= kJoinerRefreshMs);
    const bool retry =
        !g_joinerEntryAdded &&
        ((nowMs - g_lastJoinerAddAttemptMs) >= kJoinerAddRetryMs);
    if (refresh || retry) {
      g_lastJoinerAddAttemptMs = nowMs;
      tryAddJoinerEntry(refresh ? "refresh-loop" : "retry-loop", refresh);
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
