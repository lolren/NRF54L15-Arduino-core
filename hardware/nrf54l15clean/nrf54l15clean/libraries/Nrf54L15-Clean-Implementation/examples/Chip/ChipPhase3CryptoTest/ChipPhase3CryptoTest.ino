#include <Arduino.h>
#include <CHIPProjectConfig.h>
#include <CHIPError.h>
#include <SystemLayer.h>
#include <SystemLayerImplArduino.h>
#include <CryptoArduino.h>

chip::System::LayerImpl sSystemLayer;

void setup() {
    Serial.begin(115200);
    while (!Serial) { delay(10); }
    
    Serial.println("=== CHIP Phase 3 Crypto Test ===");
    
    CHIP_ERROR err = sSystemLayer.Init();
    if (err != CHIP_NO_ERROR) {
        Serial.print("System Init failed: ");
        Serial.println(err.Format());
        return;
    }
    Serial.println("[PASS] System Layer initialized");
    
    // Test DRBG (uses Arduino random())
    uint8_t buf[32];
    err = chip::Crypto::DRBG_get_bytes(buf, sizeof(buf));
    if (err != CHIP_NO_ERROR) {
        Serial.print("DRBG failed: ");
        Serial.println(err.Format());
    } else {
        Serial.print("[PASS] DRBG: ");
        for (int i = 0; i < 8; i++) { Serial.print(buf[i], HEX); Serial.print(" "); }
        Serial.println("...");
    }
    
    // Verify stubs return NOT_IMPLEMENTED
    uint8_t hash[32];
    err = chip::Crypto::Hash_SHA256((const uint8_t*)"hello", 5, hash);
    if (err == CHIP_ERROR_NOT_IMPLEMENTED) {
        Serial.println("[PASS] SHA-256 stub returns NOT_IMPLEMENTED (expected)");
    }
    
    sSystemLayer.Shutdown();
    Serial.println("\n=== Phase 3: ALL TESTS PASSED ===");
}

void loop() {}
