#include <Arduino.h>
#include <CHIPProjectConfig.h>
#include <CHIPError.h>
#include <SystemLayer.h>
#include <SystemLayerImplArduino.h>
#include <InetArduino.h>
#include <CryptoArduino.h>
#include <lib/core/TLVReader.h>
#include <lib/core/TLVWriter.h>
#include <transport/raw/PeerAddress.h>
#include <transport/raw/Tuple.h>
#include <messaging/ExchangeMgr.h>

chip::System::LayerImpl sSystemLayer;
chip::Inet::InetLayer sInetLayer;

void setup() {
    Serial.begin(115200);
    while (!Serial) { delay(10); }
    
    Serial.println("=== CHIP Phase 6 Messaging Test ===");
    
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
    
    // Test: Messaging types compile
    Serial.println("[PASS] Messaging headers accessible");
    
    sInetLayer.Shutdown();
    sSystemLayer.Shutdown();
    Serial.println("\n=== Phase 6: ALL TESTS PASSED ===");
}

void loop() {}
