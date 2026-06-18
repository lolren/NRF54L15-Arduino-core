/**
 * CHIP Phase 0 Compile Test
 *
 * Verifies that CHIPProjectConfig.h compiles correctly.
 * FQBN: nrf54l15clean:nrf54l15clean:xiao_nrf54l15:clean_thread=stage
 */

#include <Arduino.h>
#include <CHIPProjectConfig.h>

void setup() {
  Serial.begin(115200);
  while (!Serial) { delay(10); }
  
  Serial.println("=== CHIP Phase 0 Compile Test ===");
  Serial.print("CHIP_SYSTEM_CONFIG_NO_LOCKING: ");
  Serial.println(CHIP_SYSTEM_CONFIG_NO_LOCKING);
  Serial.print("CHIP_SYSTEM_CONFIG_PACKETBUFFER_POOL_SIZE: ");
  Serial.println(CHIP_SYSTEM_CONFIG_PACKETBUFFER_POOL_SIZE);
  Serial.print("CHIP_SYSTEM_CONFIG_NUM_TIMERS: ");
  Serial.println(CHIP_SYSTEM_CONFIG_NUM_TIMERS);
  Serial.print("CHIP_CONFIG_MAX_FABRICS: ");
  Serial.println(CHIP_CONFIG_MAX_FABRICS);
  Serial.print("CHIP_LOG_SIZE_OPTIMIZATION: ");
  Serial.println(CHIP_LOG_SIZE_OPTIMIZATION);
  Serial.println("=== Phase 0: PASS ===");
}

void loop() {
  // Nothing to do
}
