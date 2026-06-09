#include "matter_scenes.h"

#include <string.h>

namespace xiao_nrf54l15 {

// ─── Scene Management ──────────────────────────────────────────

int8_t MatterScenes::findScene(uint16_t groupId, uint8_t sceneId) const {
  for (uint8_t i = 0; i < kMaxScenes; i++) {
    if (scenes_[i].valid &&
        scenes_[i].groupId == groupId &&
        scenes_[i].sceneId == sceneId) {
      return static_cast<int8_t>(i);
    }
  }
  return -1;
}

uint8_t MatterScenes::findEmptySlot() const {
  for (uint8_t i = 0; i < kMaxScenes; i++) {
    if (!scenes_[i].valid) return i;
  }
  return kMaxScenes;
}

bool MatterScenes::addScene(uint16_t groupId, uint8_t sceneId,
                             const char* name,
                             const SceneExtension& extensions) {
  // Update existing or find empty slot
  int8_t idx = findScene(groupId, sceneId);
  if (idx < 0) {
    const uint8_t emptySlot = findEmptySlot();
    if (emptySlot >= kMaxScenes) return false;
    idx = static_cast<int8_t>(emptySlot);
    scenes_[idx].groupId = groupId;
    scenes_[idx].sceneId = sceneId;
  }

  SceneEntry& entry = scenes_[idx];
  if (name != nullptr) {
    strncpy(entry.name, name, kSceneNameSize - 1);
    entry.name[kSceneNameSize - 1] = '\0';
  } else {
    entry.name[0] = '\0';
  }
  entry.extensions = extensions;
  entry.valid = true;
  sceneCount_++;

  // Re-count actual valid entries
  uint8_t actualCount = 0;
  for (uint8_t i = 0; i < kMaxScenes; i++) {
    if (scenes_[i].valid) actualCount++;
  }
  sceneCount_ = actualCount;

  return true;
}

bool MatterScenes::viewScene(uint16_t groupId, uint8_t sceneId,
                              SceneEntry* outEntry) const {
  if (outEntry == nullptr) return false;
  const int8_t idx = findScene(groupId, sceneId);
  if (idx < 0) return false;
  *outEntry = scenes_[idx];
  return true;
}

bool MatterScenes::removeScene(uint16_t groupId, uint8_t sceneId) {
  const int8_t idx = findScene(groupId, sceneId);
  if (idx < 0) return false;
  memset(&scenes_[idx], 0, sizeof(scenes_[idx]));
  sceneCount_--;
  return true;
}

bool MatterScenes::recallScene(uint16_t groupId, uint8_t sceneId,
                                SceneExtension* outExtensions) const {
  if (outExtensions == nullptr) return false;
  const int8_t idx = findScene(groupId, sceneId);
  if (idx < 0) return false;
  *outExtensions = scenes_[idx].extensions;
  return true;
}

bool MatterScenes::storeScene(uint16_t groupId, uint8_t sceneId,
                               const SceneExtension& currentExtensions) {
  const int8_t idx = findScene(groupId, sceneId);
  if (idx < 0) return false;
  scenes_[idx].extensions = currentExtensions;
  return true;
}

bool MatterScenes::getSceneByIndex(uint8_t index, SceneEntry* outEntry) const {
  if (outEntry == nullptr) return false;
  uint8_t found = 0;
  for (uint8_t i = 0; i < kMaxScenes; i++) {
    if (scenes_[i].valid) {
      if (found == index) {
        *outEntry = scenes_[i];
        return true;
      }
      found++;
    }
  }
  return false;
}

bool MatterScenes::removeAllScenes(uint16_t groupId) {
  bool anyRemoved = false;
  for (uint8_t i = 0; i < kMaxScenes; i++) {
    if (scenes_[i].valid && scenes_[i].groupId == groupId) {
      memset(&scenes_[i], 0, sizeof(scenes_[i]));
      anyRemoved = true;
    }
  }
  if (anyRemoved) {
    sceneCount_ = 0;
    for (uint8_t i = 0; i < kMaxScenes; i++) {
      if (scenes_[i].valid) sceneCount_++;
    }
  }
  return anyRemoved;
}

void MatterScenes::clear() {
  memset(scenes_, 0, sizeof(scenes_));
  sceneCount_ = 0U;
}

bool MatterScenes::sceneExists(uint16_t groupId, uint8_t sceneId) const {
  return findScene(groupId, sceneId) >= 0;
}

}  // namespace xiao_nrf54l15