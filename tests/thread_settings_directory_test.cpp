#include <algorithm>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <map>
#include <string>
#include <utility>
#include <vector>

// This backend injects only failures reported by Preferences API calls. It
// intentionally does not model torn RRAM writes, resets, or brownouts.
struct otInstance {};

enum otError {
  OT_ERROR_NONE = 0,
  OT_ERROR_NO_BUFS = 3,
  OT_ERROR_INVALID_ARGS = 7,
  OT_ERROR_NOT_FOUND = 23,
};

class Preferences {
 public:
  enum class Kind {
    kBytes,
    kUShort,
  };

  struct Entry {
    Kind kind = Kind::kBytes;
    std::vector<uint8_t> bytes;
    uint16_t ushortValue = 0U;
  };

  bool begin(const char*, bool) {
    open_ = beginResult_;
    return open_;
  }

  void end() { open_ = false; }

  bool isKey(const char* key) const {
    return key != nullptr && entries_.find(key) != entries_.end();
  }

  size_t getBytesLength(const char* key) const {
    const Entry* entry = find(key);
    return entry != nullptr && entry->kind == Kind::kBytes
               ? entry->bytes.size()
               : 0U;
  }

  size_t getBytes(const char* key, void* output, size_t maxLength) const {
    const Entry* entry = find(key);
    if (entry == nullptr || entry->kind != Kind::kBytes ||
        (output == nullptr && maxLength != 0U)) {
      return 0U;
    }
    const size_t copied = std::min(maxLength, entry->bytes.size());
    if (copied != 0U) {
      memcpy(output, entry->bytes.data(), copied);
    }
    return copied;
  }

  uint16_t getUShort(const char* key, uint16_t defaultValue) const {
    const Entry* entry = find(key);
    return entry != nullptr && entry->kind == Kind::kUShort
               ? entry->ushortValue
               : defaultValue;
  }

  size_t putBytes(const char* key, const void* value, size_t length) {
    if (!open_ || key == nullptr || (value == nullptr && length != 0U) ||
        failPut()) {
      return 0U;
    }
    Entry entry;
    entry.kind = Kind::kBytes;
    const auto* bytes = static_cast<const uint8_t*>(value);
    if (length != 0U) {
      entry.bytes.assign(bytes, bytes + length);
    }
    entries_[key] = std::move(entry);
    return length;
  }

  size_t putUShort(const char* key, uint16_t value) {
    if (!open_ || key == nullptr || failPut()) {
      return 0U;
    }
    Entry entry;
    entry.kind = Kind::kUShort;
    entry.ushortValue = value;
    entries_[key] = std::move(entry);
    return sizeof(value);
  }

  bool remove(const char* key) {
    ++removeCalls_;
    if (!open_ || key == nullptr || failAllRemoves_ ||
        (!failRemoveKey_.empty() && failRemoveKey_ == key)) {
      return false;
    }
    return entries_.erase(key) != 0U;
  }

  bool clear() {
    if (!open_) {
      return false;
    }
    entries_.clear();
    return true;
  }

  size_t freeEntries() const {
    return entries_.size() < kCapacity ? kCapacity - entries_.size() : 0U;
  }

  void seedBytes(const std::string& key, const std::vector<uint8_t>& value) {
    Entry entry;
    entry.kind = Kind::kBytes;
    entry.bytes = value;
    entries_[key] = std::move(entry);
  }

  void seedUShort(const std::string& key, uint16_t value) {
    Entry entry;
    entry.kind = Kind::kUShort;
    entry.ushortValue = value;
    entries_[key] = std::move(entry);
  }

  const Entry* find(const char* key) const {
    if (key == nullptr) {
      return nullptr;
    }
    const auto it = entries_.find(key);
    return it == entries_.end() ? nullptr : &it->second;
  }

  void failPutOnCall(size_t call) { failPutCall_ = call; }
  size_t putCalls() const { return putCalls_; }
  void failAllRemoves(bool fail) { failAllRemoves_ = fail; }
  void failRemoveFor(const std::string& key) { failRemoveKey_ = key; }
  void clearRemoveFailure() {
    failAllRemoves_ = false;
    failRemoveKey_.clear();
  }

 private:
  bool failPut() {
    ++putCalls_;
    if (failPutCall_ != 0U && putCalls_ == failPutCall_) {
      failPutCall_ = 0U;
      return true;
    }
    return false;
  }

  static constexpr size_t kCapacity = 256U;
  std::map<std::string, Entry> entries_;
  bool open_ = false;
  bool beginResult_ = true;
  size_t putCalls_ = 0U;
  size_t failPutCall_ = 0U;
  size_t removeCalls_ = 0U;
  bool failAllRemoves_ = false;
  std::string failRemoveKey_;
};

namespace xiao_nrf54l15 {
namespace {

constexpr const char* kSettingsNamespace = "otplat";

struct TestSettingsSnapshot {
  bool settingsInitialized = false;
  uint16_t sensitiveKeyCount = 0U;
  uint16_t lastSettingsKey = 0U;
  uint16_t lastSettingsLength = 0U;
  uint16_t settingsKeyCount = 0U;
};

struct OpenThreadPlatformState {
  TestSettingsSnapshot snapshot;
  Preferences settings;
  bool settingsOpen = false;
  const uint16_t* sensitiveKeys = nullptr;
} gOpenThreadPlatformState;

#include "thread_settings_helpers.inc"

}  // namespace
}  // namespace xiao_nrf54l15

extern "C" {
#include "thread_settings_apis.inc"
}

namespace {

using namespace xiao_nrf54l15;

bool expect(bool condition, const char* message) {
  if (condition) {
    return true;
  }
  fprintf(stderr, "FAIL: %s\n", message);
  return false;
}

std::string countKey(uint16_t key) {
  char output[16];
  makeCountKey(key, output, sizeof(output));
  return output;
}

std::string dataKey(uint16_t key, uint16_t index) {
  char output[20];
  makeDataKey(key, index, output, sizeof(output));
  return output;
}

std::string chunkKey(uint16_t key, uint16_t index, uint16_t chunk) {
  char output[20];
  makeDataChunkKey(key, index, chunk, output, sizeof(output));
  return output;
}

std::vector<uint8_t> bytes(const char* text) {
  return std::vector<uint8_t>(text, text + strlen(text));
}

void resetStore() {
  closeSettings();
  gOpenThreadPlatformState = OpenThreadPlatformState{};
  (void)ensureSettingsOpen();
}

void seedLegacy(uint16_t key,
                const std::vector<std::vector<uint8_t>>& values) {
  gOpenThreadPlatformState.settings.seedUShort(
      countKey(key), static_cast<uint16_t>(values.size()));
  for (size_t index = 0U; index < values.size(); ++index) {
    gOpenThreadPlatformState.settings.seedBytes(
        dataKey(key, static_cast<uint16_t>(index)), values[index]);
  }
}

bool readEquals(uint16_t key, int index,
                const std::vector<uint8_t>& expected) {
  uint8_t output[128] = {};
  uint16_t length = sizeof(output);
  if (otPlatSettingsGet(nullptr, key, index, output, &length) !=
      OT_ERROR_NONE) {
    return false;
  }
  return length == expected.size() &&
         memcmp(output, expected.data(), expected.size()) == 0;
}

bool isNotFound(uint16_t key, int index) {
  uint16_t length = 0U;
  return otPlatSettingsGet(nullptr, key, index, nullptr, &length) ==
         OT_ERROR_NOT_FOUND;
}

bool testLegacyMigrationAndMutations() {
  constexpr uint16_t key = 0x1234U;
  const std::vector<uint8_t> alpha = bytes("alpha");
  const std::vector<uint8_t> beta = bytes("beta");
  const std::vector<uint8_t> gamma = bytes("gamma");
  const std::vector<uint8_t> replacement = bytes("replacement");
  bool ok = true;

  resetStore();
  seedLegacy(key, {alpha, beta});
  ok &= expect(readEquals(key, 0, alpha) && readEquals(key, 1, beta),
               "legacy ushort directory must remain readable");
  ok &= expect(otPlatSettingsAdd(nullptr, key, gamma.data(), gamma.size()) ==
                   OT_ERROR_NONE,
               "adding to a legacy record must succeed");
  ok &= expect(gOpenThreadPlatformState.settings.getBytesLength(
                   countKey(key).c_str()) == kSettingDirectoryLength,
               "the first mutation must migrate the legacy count to OTD1");
  ok &= expect(readEquals(key, 0, alpha) && readEquals(key, 1, beta) &&
                   readEquals(key, 2, gamma),
               "legacy migration must preserve logical ordering");

  ok &= expect(otPlatSettingsSet(nullptr, key, replacement.data(),
                                 replacement.size()) == OT_ERROR_NONE,
               "set must replace every prior logical value");
  ok &= expect(readEquals(key, 0, replacement) && isNotFound(key, 1),
               "set must expose only the replacement value");
  ok &= expect(otPlatSettingsAdd(nullptr, key, gamma.data(), gamma.size()) ==
                   OT_ERROR_NONE,
               "add must append after a set");
  ok &= expect(otPlatSettingsDelete(nullptr, key, 0) == OT_ERROR_NONE,
               "indexed delete must succeed");
  ok &= expect(readEquals(key, 0, gamma) && isNotFound(key, 1),
               "indexed delete must retain and renumber later values");
  ok &= expect(otPlatSettingsDelete(nullptr, key, -1) == OT_ERROR_NONE,
               "delete-all must succeed");
  ok &= expect(isNotFound(key, 0) &&
                   !gOpenThreadPlatformState.settings.isKey(
                       countKey(key).c_str()),
               "delete-all must hide values and reclaim the directory");
  ok &= expect(wipeSettingsStore(),
               "the settings backend must remain wipeable after cleanup");
  return ok;
}

bool testDirectoryCommitPutFailureKeepsOldMapping() {
  constexpr uint16_t key = 0x2222U;
  const std::vector<uint8_t> oldValue = bytes("old-visible-value");
  const std::vector<uint8_t> newValue = bytes("new-staged-value");
  bool ok = true;

  resetStore();
  ok &= expect(otPlatSettingsSet(nullptr, key, oldValue.data(),
                                 oldValue.size()) == OT_ERROR_NONE,
               "test setup set must succeed");

  SettingDirectory before;
  ok &= expect(loadSettingDirectory(key, &before),
               "committed directory must load before fault injection");
  const size_t putBase = gOpenThreadPlatformState.settings.putCalls();
  // pending directory, staged compact value, committed directory
  gOpenThreadPlatformState.settings.failPutOnCall(putBase + 3U);
  ok &= expect(otPlatSettingsSet(nullptr, key, newValue.data(),
                                 newValue.size()) == OT_ERROR_NO_BUFS,
               "a failed visibility commit must be reported");
  ok &= expect(readEquals(key, 0, oldValue) && isNotFound(key, 1),
               "a failed visibility commit must retain the prior value");

  SettingDirectory after;
  ok &= expect(loadSettingDirectory(key, &after) &&
                   after.count == before.count &&
                   after.pendingIndex == kSettingInvalidPhysicalIndex &&
                   memcmp(after.physicalIndices, before.physicalIndices,
                          sizeof(after.physicalIndices)) == 0,
               "commit failure cleanup must restore the prior mapping");
  return ok;
}

bool testPendingRecoveryAfterPutAndRemoveFailures() {
  constexpr uint16_t key = 0x3333U;
  const std::vector<uint8_t> oldValue = bytes("stable");
  std::vector<uint8_t> stagedValue(70U);
  for (size_t i = 0U; i < stagedValue.size(); ++i) {
    stagedValue[i] = static_cast<uint8_t>(i + 1U);
  }
  bool ok = true;

  resetStore();
  ok &= expect(otPlatSettingsSet(nullptr, key, oldValue.data(),
                                 oldValue.size()) == OT_ERROR_NONE,
               "pending-recovery setup must succeed");
  SettingDirectory before;
  ok &= expect(loadSettingDirectory(key, &before),
               "pending-recovery setup directory must load");

  const size_t putBase = gOpenThreadPlatformState.settings.putCalls();
  // pending directory, first data chunk, failed second data chunk
  gOpenThreadPlatformState.settings.failPutOnCall(putBase + 3U);
  gOpenThreadPlatformState.settings.failAllRemoves(true);
  ok &= expect(otPlatSettingsSet(nullptr, key, stagedValue.data(),
                                 stagedValue.size()) == OT_ERROR_NO_BUFS,
               "staged write and cleanup failures must be reported");

  SettingDirectory pending;
  ok &= expect(loadSettingDirectory(key, &pending) &&
                   pending.count == before.count &&
                   pending.pendingIndex != kSettingInvalidPhysicalIndex &&
                   pending.pendingLength == stagedValue.size(),
               "failed cleanup must retain ownership of the pending slot");
  ok &= expect(readSettingItem(key, pending.physicalIndices[0], nullptr,
                               &pending.pendingLength),
               "the prior mapped item must remain readable during recovery");
  const uint8_t pendingIndex = pending.pendingIndex;
  ok &= expect(gOpenThreadPlatformState.settings.isKey(
                   chunkKey(key, pendingIndex, 0U).c_str()),
               "the partially written chunk must remain discoverable");

  gOpenThreadPlatformState.settings.clearRemoveFailure();
  ok &= expect(readEquals(key, 0, oldValue),
               "a later read must recover garbage without changing visibility");
  SettingDirectory recovered;
  ok &= expect(loadSettingDirectory(key, &recovered) &&
                   recovered.pendingIndex == kSettingInvalidPhysicalIndex &&
                   recovered.pendingLength == 0U &&
                   !gOpenThreadPlatformState.settings.isKey(
                       chunkKey(key, pendingIndex, 0U).c_str()),
               "successful recovery must clear pending ownership and chunks");
  return ok;
}

bool testPendingRemoveFailureBlocksMutationWithoutChangingMapping() {
  constexpr uint16_t key = 0x4444U;
  const std::vector<uint8_t> oldValue = bytes("old");
  const std::vector<uint8_t> appended = bytes("append");
  bool ok = true;

  resetStore();
  ok &= expect(otPlatSettingsSet(nullptr, key, oldValue.data(),
                                 oldValue.size()) == OT_ERROR_NONE,
               "remove-failure setup must succeed");
  SettingDirectory directory;
  ok &= expect(loadSettingDirectory(key, &directory),
               "remove-failure setup directory must load");
  uint8_t pendingIndex = kSettingInvalidPhysicalIndex;
  ok &= expect(findFreeSettingPhysicalIndex(directory, &pendingIndex),
               "remove-failure setup needs a free physical slot");
  directory.pendingIndex = pendingIndex;
  directory.pendingLength = 4U;
  gOpenThreadPlatformState.settings.seedBytes(dataKey(key, pendingIndex),
                                               bytes("junk"));
  ok &= expect(saveSettingDirectory(key, directory),
               "remove-failure pending directory must persist");

  gOpenThreadPlatformState.settings.failRemoveFor(dataKey(key, pendingIndex));
  ok &= expect(otPlatSettingsAdd(nullptr, key, appended.data(),
                                 appended.size()) == OT_ERROR_NO_BUFS,
               "pending cleanup remove failure must block a mutation");
  SettingDirectory stillPending;
  ok &= expect(loadSettingDirectory(key, &stillPending) &&
                   stillPending.count == 1U &&
                   stillPending.pendingIndex == pendingIndex,
               "a remove failure must retain the prior mapping and owner");
  gOpenThreadPlatformState.settings.clearRemoveFailure();
  ok &= expect(readEquals(key, 0, oldValue) && isNotFound(key, 1),
               "failed pending cleanup must not expose the attempted append");
  return ok;
}

}  // namespace

int main() {
  bool ok = true;
  ok &= testLegacyMigrationAndMutations();
  ok &= testDirectoryCommitPutFailureKeepsOldMapping();
  ok &= testPendingRecoveryAfterPutAndRemoveFailures();
  ok &= testPendingRemoveFailureBlocksMutationWithoutChangingMapping();
  if (ok) {
    puts("PASS Thread settings directory fault recovery");
  }
  return ok ? 0 : 1;
}
