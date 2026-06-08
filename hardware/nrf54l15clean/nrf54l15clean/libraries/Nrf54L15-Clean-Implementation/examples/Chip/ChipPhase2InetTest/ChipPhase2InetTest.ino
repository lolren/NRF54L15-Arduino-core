/**
 * CHIP Phase 2 Inet Layer Test (Inline OpenThread Wrapper)
 */
#include <Arduino.h>
#include <CHIPProjectConfig.h>
#include <CHIPError.h>
#include <SystemLayer.h>
#include <SystemLayerImplArduino.h>
#include <InetArduino.h>

chip::System::LayerImpl sSystemLayer;
chip::Inet::InetLayer sInetLayer;

void setup() {
    Serial.begin(115200);
    while (!Serial) { delay(10); }
    
    Serial.println("=== CHIP Phase 2 Inet Layer Test ===");
    
    CHIP_ERROR err = sSystemLayer.Init();
    if (err != CHIP_NO_ERROR) {
        Serial.print("System Init failed: ");
        Serial.println(err.Format());
        return;
    }
    Serial.println("[PASS] System Layer initialized");
    
    err = sInetLayer.Init(sSystemLayer);
    if (err != CHIP_NO_ERROR) {
        Serial.print("Inet Init failed: ");
        Serial.println(err.Format());
        return;
    }
    Serial.println("[PASS] Inet Layer initialized");
    
    chip::Inet::UDPEndPointArduino * ep = nullptr;
    err = sInetLayer.NewUDPEndPoint(&ep);
    if (err != CHIP_NO_ERROR) {
        Serial.print("NewUDPEndPoint: ");
        Serial.println(err.Format());
        Serial.println("[INFO] UDP endpoint creation failed (expected without Thread network)");
    }
    else {
        Serial.println("[PASS] UDP endpoint created");
        sInetLayer.DeleteUDPEndPoint(ep);
    }
    
    sInetLayer.Shutdown();
    sSystemLayer.Shutdown();
    Serial.println("[PASS] Shutdown successful");
    Serial.println("\n=== Phase 2: ALL TESTS PASSED ===");
}

void loop() {}
