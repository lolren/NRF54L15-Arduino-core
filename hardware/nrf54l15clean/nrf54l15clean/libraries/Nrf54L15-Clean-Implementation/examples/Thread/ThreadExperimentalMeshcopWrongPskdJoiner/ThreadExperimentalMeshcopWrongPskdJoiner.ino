#include <nrf54_all.h>

#if !defined(NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE) || \
    (NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE == 0)
#error "Enable Tools > Thread Core > Experimental Stage Core before building this example."
#endif

namespace {

constexpr char kWrongJoinerPskd[] = "BADPSK54";
constexpr uint32_t kStatusPrintIntervalMs = 3000U;
constexpr uint32_t kJoinerStartRetryMs = 2000U;

xiao_nrf54l15::Nrf54ThreadExperimental g_thread;
uint32_t g_lastStatusPrintMs = 0U;
uint32_t g_lastJoinerStartAttemptMs = 0U;
bool g_joinerStarted = false;
bool g_joinerCallbackSeen = false;
bool g_unexpectedSuccess = false;
bool g_cleanDatasetChecked = false;
bool g_blockedByPreexistingDataset = false;

bool activeDatasetPresent(uint16_t* outLength = nullptr) {
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

void printStatus(const char* reason) {
  uint16_t datasetLength = 0U;
  const bool haveDataset = activeDatasetPresent(&datasetLength);

  Serial.print("thread_meshcop_wrong_pskd reason=");
  Serial.print(reason);
  Serial.print(" started=");
  Serial.print(g_thread.started() ? 1 : 0);
  Serial.print(" attached=");
  Serial.print(g_thread.attached() ? 1 : 0);
  Serial.print(" role=");
  Serial.print(g_thread.roleName());
  Serial.print(" rloc16=0x");
  Serial.print(g_thread.rloc16(), HEX);
  Serial.print(" joiner_supported=");
  Serial.print(g_thread.joinerSupported() ? 1 : 0);
  Serial.print(" joiner_active=");
  Serial.print(g_thread.joinerActive() ? 1 : 0);
  Serial.print(" joiner_state=");
  Serial.print(g_thread.joinerStateName());
  Serial.print(" callback_seen=");
  Serial.print(g_joinerCallbackSeen ? 1 : 0);
  Serial.print(" unexpected_success=");
  Serial.print(g_unexpectedSuccess ? 1 : 0);
  Serial.print(" active_dataset=");
  Serial.print(haveDataset ? 1 : 0);
  Serial.print(" active_dataset_tlv_len=");
  Serial.print(datasetLength);
  Serial.print(" last_error=");
  Serial.println(static_cast<int>(g_thread.lastError()));
}

void onJoinerCallback(void*, otError error) {
  g_joinerCallbackSeen = true;
  Serial.print("thread_meshcop_wrong_pskd joiner_callback error=");
  Serial.println(static_cast<int>(error));

  if (error == OT_ERROR_NONE) {
    g_unexpectedSuccess = true;
    Serial.println("thread_meshcop_wrong_pskd FATAL unexpected_join_success=1");
  } else {
    Serial.println("thread_meshcop_wrong_pskd expected_join_failure=1");
  }
  printStatus("joiner-callback");
}

}  // namespace

void setup() {
  Serial.begin(115200);
  const uint32_t waitStart = millis();
  while (!Serial && (millis() - waitStart) < 1500UL) {
  }

  Serial.println("thread_meshcop_wrong_pskd Wrong-PSKd Joiner test starting...");
  Serial.println("thread_meshcop_wrong_pskd wipe_requested=1");
  Serial.println("thread_meshcop_wrong_pskd negative_test=1");
  Serial.println("thread_meshcop_wrong_pskd expected_result=joiner_failure_no_dataset");
  Serial.print("thread_meshcop_wrong_pskd wrong_pskd=");
  Serial.println(kWrongJoinerPskd);
  Serial.print("thread_meshcop_wrong_pskd standard_meshcop_enabled=");
  Serial.println(g_thread.joinerSupported() ? 1 : 0);

  const bool beginOk = g_thread.beginJoinerOnly(true);
  Serial.print("thread_meshcop_wrong_pskd begin=");
  Serial.println(beginOk ? 1 : 0);
  if (!beginOk) {
    Serial.println("thread_meshcop_wrong_pskd FATAL begin failed");
    return;
  }

  printStatus("boot");
}

void loop() {
  g_thread.process();

  if (g_blockedByPreexistingDataset || g_unexpectedSuccess) {
    if ((millis() - g_lastStatusPrintMs) >= kStatusPrintIntervalMs) {
      g_lastStatusPrintMs = millis();
      printStatus(g_blockedByPreexistingDataset ? "blocked-preexisting-dataset"
                                                : "unexpected-success");
    }
    return;
  }

  if (!g_joinerStarted && !g_joinerCallbackSeen &&
      g_thread.rawInstance() != nullptr &&
      (millis() - g_lastJoinerStartAttemptMs) >= kJoinerStartRetryMs) {
    g_lastJoinerStartAttemptMs = millis();

    if (!g_cleanDatasetChecked) {
      uint16_t datasetLength = 0U;
      const bool haveDataset = activeDatasetPresent(&datasetLength);
      g_cleanDatasetChecked = true;
      Serial.print("thread_meshcop_wrong_pskd before_joiner_active_dataset=");
      Serial.println(haveDataset ? 1 : 0);
      Serial.print("thread_meshcop_wrong_pskd before_joiner_active_dataset_tlv_len=");
      Serial.println(datasetLength);
      if (haveDataset) {
        g_blockedByPreexistingDataset = true;
        Serial.println(
            "thread_meshcop_wrong_pskd FATAL preexisting_dataset_before_joiner=1");
        return;
      }
    }

    const bool joinerOk =
        g_thread.startJoiner(kWrongJoinerPskd, nullptr, onJoinerCallback,
                             nullptr);
    Serial.print("thread_meshcop_wrong_pskd joiner_start=");
    Serial.println(joinerOk ? 1 : 0);
    if (!joinerOk) {
      Serial.print("thread_meshcop_wrong_pskd joiner_error=");
      Serial.println(static_cast<int>(g_thread.lastError()));
    } else {
      g_joinerStarted = true;
    }
  }

  if ((millis() - g_lastStatusPrintMs) >= kStatusPrintIntervalMs) {
    g_lastStatusPrintMs = millis();
    printStatus("heartbeat");
  }
}
