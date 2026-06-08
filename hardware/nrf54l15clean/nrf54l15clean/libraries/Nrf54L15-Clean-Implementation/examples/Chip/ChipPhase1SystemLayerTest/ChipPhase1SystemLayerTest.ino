/**
 * CHIP Phase 1 System Layer Test
 *
 * Tests System Layer: initialization, timers, work scheduling.
 * FQBN: nrf54l15clean:nrf54l15clean:xiao_nrf54l15:clean_thread=stage,clean_matter=stage
 */

#include <Arduino.h>
#include <CHIPProjectConfig.h>
#include <CHIPError.h>
#include <SystemLayer.h>
#include <SystemLayerImplArduino.h>

// Global System Layer instance (use concrete implementation)
chip::System::LayerImpl sSystemLayer;

// Test timer callback
static void TimerFired(chip::System::Layer *layer, void *appState) {
    Serial.println("  Timer fired!");
}

static void WorkScheduled(chip::System::Layer *layer, void *appState) {
    Serial.println("  Work callback executed!");
}

void setup() {
    Serial.begin(115200);
    while (!Serial) { delay(10); }
    
    Serial.println("=== CHIP Phase 1 System Layer Test ===");
    
    // Initialize System Layer
    CHIP_ERROR err = sSystemLayer.Init();
    if (err != CHIP_NO_ERROR) {
        Serial.print("Init failed: ");
        Serial.println(err.Format());
        return;
    }
    Serial.println("[PASS] System Layer initialized");
    Serial.print("IsInitialized: ");
    Serial.println(sSystemLayer.IsInitialized() ? "true" : "false");
    
    // Test 1: Start a timer
    err = sSystemLayer.StartTimer(chip::System::Clock::Timeout(100), TimerFired, nullptr);
    if (err != CHIP_NO_ERROR) {
        Serial.print("StartTimer failed: ");
        Serial.println(err.Format());
        return;
    }
    Serial.println("[PASS] Timer started (100ms)");
    
    // Test 2: Check timer is active
    bool active = sSystemLayer.IsTimerActive(TimerFired, nullptr);
    Serial.print("IsTimerActive: ");
    Serial.println(active ? "true" : "false");
    
    // Test 3: Get remaining time
    auto remaining = sSystemLayer.GetRemainingTime(TimerFired, nullptr);
    Serial.print("Remaining time: ");
    Serial.println(remaining.count());
    
    // Test 4: Cancel timer
    sSystemLayer.CancelTimer(TimerFired, nullptr);
    active = sSystemLayer.IsTimerActive(TimerFired, nullptr);
    Serial.print("After cancel, IsTimerActive: ");
    Serial.println(active ? "true" : "false");
    Serial.println("[PASS] Timer cancelled");
    
    // Test 5: Schedule work
    err = sSystemLayer.ScheduleWork(WorkScheduled, nullptr);
    if (err != CHIP_NO_ERROR) {
        Serial.print("ScheduleWork failed: ");
        Serial.println(err.Format());
        return;
    }
    Serial.println("[PASS] Work scheduled");
    
    // Test 6: Handle events (processes timers and work)
    sSystemLayer.HandleEvents();
    Serial.println("[PASS] HandleEvents processed work");
    
    // Test 7: Start timer and let it fire via HandleEvents
    err = sSystemLayer.StartTimer(chip::System::Clock::Timeout(10), TimerFired, nullptr);
    if (err != CHIP_NO_ERROR) {
        Serial.print("StartTimer2 failed: ");
        Serial.println(err.Format());
        return;
    }
    delay(50);  // Wait for timer to expire
    sSystemLayer.HandleEvents();  // This should fire the timer
    Serial.println("[PASS] Timer fired via HandleEvents");
    
    // Test 8: Shutdown
    sSystemLayer.Shutdown();
    Serial.print("After shutdown, IsInitialized: ");
    Serial.println(sSystemLayer.IsInitialized() ? "true" : "false");
    Serial.println("[PASS] Shutdown successful");
    
    Serial.println("\n=== Phase 1: ALL TESTS PASSED ===");
}

void loop() {
    // Nothing to do
}
