#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace xiao_nrf54l15 {

// Matter Scenes Cluster (0x0005) Implementation
// Stores scenes with on/off state and level for recall

constexpr uint8_t kMaxScenes = 16U;
constexpr uint8_t kSceneNameSize = 16U;
constexpr uint16_t kSceneClusterId = 0x0005U;

// Scene extension field sets (what we store per scene)
struct SceneExtension {
  bool on = false;           // On/Off cluster state
  uint8_t level = 255U;      // Level Control cluster state (0-254+255)
  uint8_t transitionTime = 0U;  // Transition time in seconds
};

struct SceneEntry {
  uint16_t groupId = 0U;
  uint8_t sceneId = 0U;
  char name[kSceneNameSize] = {0};
  SceneExtension extensions;
  bool valid = false;
};

class MatterScenes {
 public:
  MatterScenes() = default;

  // Add or update a scene
  bool addScene(uint16_t groupId, uint8_t sceneId,
                const char* name, const SceneExtension& extensions);

  // View a scene
  bool viewScene(uint16_t groupId, uint8_t sceneId,
                 SceneEntry* outEntry) const;

  // Remove a scene
  bool removeScene(uint16_t groupId, uint8_t sceneId);

  // Recall a scene (apply stored state)
  bool recallScene(uint16_t groupId, uint8_t sceneId,
                   SceneExtension* outExtensions) const;

  // Store current state as scene
  bool storeScene(uint16_t groupId, uint8_t sceneId,
                  const SceneExtension& currentExtensions);

  // Get scene count
  uint8_t sceneCount() const { return sceneCount_; }

  // Get a scene by index
  bool getSceneByIndex(uint8_t index, SceneEntry* outEntry) const;

  // Remove all scenes for a group
  bool removeAllScenes(uint16_t groupId);

  // Clear all scenes
  void clear();

  // Check if a scene exists
  bool sceneExists(uint16_t groupId, uint8_t sceneId) const;

  // Get the maximum number of scenes
  static uint8_t maxScenes() { return kMaxScenes; }

 private:
  SceneEntry scenes_[kMaxScenes];
  uint8_t sceneCount_ = 0U;

  int8_t findScene(uint16_t groupId, uint8_t sceneId) const;
  uint8_t findEmptySlot() const;
};

}  // namespace xiao_nrf54l15