#include <nrf54_thread_experimental.h>

using xiao_nrf54l15::Nrf54ThreadExperimental;

#if !defined(NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE) || \
    (NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE == 0)
#error "Enable Tools > Thread Core > Experimental Stage Core before building this example."
#endif

namespace {

// This probe is intentionally restore-only:
// - do not wipe settings
// - do not seed a demo dataset
// - do not start Joiner
// Flash it after a successful MeshCoP Joiner run to prove the commissioned
// operational dataset was persisted and can reattach without sketch injection.
constexpr uint32_t kStatusIntervalMs = 1000UL;

Nrf54ThreadExperimental g_thread;
Nrf54ThreadExperimental::Role g_lastRole =
    Nrf54ThreadExperimental::Role::kUnknown;
uint32_t g_lastStatusMs = 0U;
bool g_datasetHexPrinted = false;

void printDatasetHexOnce() {
  if (g_datasetHexPrinted) {
    return;
  }

  char datasetHex[(OT_OPERATIONAL_DATASET_MAX_LENGTH * 2U) + 1U] = {0};
  size_t hexLength = 0U;
  if (!g_thread.exportConfiguredOrActiveDatasetHex(
          datasetHex, sizeof(datasetHex), &hexLength)) {
    return;
  }

  Serial.print("thread_meshcop_restore dataset_hex_len=");
  Serial.println(static_cast<unsigned>(hexLength));
  Serial.print("thread_meshcop_restore dataset_hex=");
  Serial.println(datasetHex);
  g_datasetHexPrinted = true;
}

void printStatus(const char* reason) {
  Nrf54ThreadExperimental::DatasetRestoreDiagnostics restore = {};
  (void)g_thread.getDatasetRestoreDiagnostics(&restore);

  Nrf54ThreadExperimental::AttachSummary attach = {};
  (void)g_thread.getAttachSummary(&attach);

  Serial.print("thread_meshcop_restore reason=");
  Serial.print(reason);
  Serial.print(" started=");
  Serial.print(g_thread.started() ? 1 : 0);
  Serial.print(" attached=");
  Serial.print(g_thread.attached() ? 1 : 0);
  Serial.print(" role=");
  Serial.print(g_thread.roleName());
  Serial.print(" rloc16=0x");
  Serial.print(g_thread.rloc16(), HEX);
  Serial.print(" partition=");
  Serial.print(g_thread.partitionId());
  Serial.print(" dataset_configured=");
  Serial.print(g_thread.datasetConfigured() ? 1 : 0);
  Serial.print(" restored_from_settings=");
  Serial.print(g_thread.restoredFromSettings() ? 1 : 0);
  Serial.print(" last_error=");
  Serial.print(static_cast<int>(g_thread.lastError()));
  Serial.print(" restore_valid=");
  Serial.print(restore.valid ? 1 : 0);
  Serial.print(" restore_attempted=");
  Serial.print(restore.attempted ? 1 : 0);
  Serial.print(" restore_restored=");
  Serial.print(restore.restored ? 1 : 0);
  Serial.print(" restore_source=");
  Serial.print(restore.sourceName);
  Serial.print(" restore_tlv_len=");
  Serial.print(restore.restoredTlvLength);
  Serial.print(" restore_error=");
  Serial.print(static_cast<int>(restore.error));
  Serial.print(" restore_blocker=");
  Serial.print(restore.blockerName);
  Serial.print(" attach_phase=");
  Serial.print(attach.phaseName);
  Serial.print(" attach_blocker=");
  Serial.println(attach.blockerName);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  const uint32_t waitStart = millis();
  while (!Serial && (millis() - waitStart) < 1500UL) {
  }

  Serial.println("thread_meshcop_restore Restore-only MeshCoP probe starting...");
  Serial.println("thread_meshcop_restore wipe_requested=0");
  Serial.println("thread_meshcop_restore seed_demo_dataset=0");
  Serial.println("thread_meshcop_restore joiner_start=0");
  Serial.println(
      "thread_meshcop_restore expected_after_successful_joiner="
      "restore_attempted=1 restore_restored=1 dataset_configured=1");

  const bool beginOk = g_thread.begin(false);
  Serial.print("thread_meshcop_restore begin=");
  Serial.println(beginOk ? 1 : 0);
  if (!beginOk) {
    Serial.println("thread_meshcop_restore FATAL begin failed");
    return;
  }

  printStatus("boot");
}

void loop() {
  g_thread.process();
  printDatasetHexOnce();

  const Nrf54ThreadExperimental::Role currentRole = g_thread.role();
  const uint32_t nowMs = millis();
  const bool roleChanged = (currentRole != g_lastRole);
  if (roleChanged || (nowMs - g_lastStatusMs) >= kStatusIntervalMs) {
    g_lastRole = currentRole;
    g_lastStatusMs = nowMs;
    printStatus(roleChanged ? "role" : "status");
  }
}
