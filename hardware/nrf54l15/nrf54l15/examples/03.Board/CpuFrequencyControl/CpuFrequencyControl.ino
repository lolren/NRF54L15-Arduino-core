/*
 * CpuFrequencyControl
 *
 * Shows CPU frequency selected by Tools and current hardware frequency.
 * Optional: request 64 MHz or 128 MHz at runtime.
 */

#include <XiaoNrf54L15.h>

// Set to 0, 64000000, or 128000000.
#define TARGET_RUNTIME_CPU_HZ 0UL

static void printHz(const char* label, uint32_t hz)
{
  Serial.print(label);
  Serial.print(": ");
  Serial.print(hz);
  Serial.println(" Hz");
}

void setup()
{
  Serial.begin(115200);
  delay(300);

  Serial.println("cpu-frequency-control: ready");
  printHz("tools-f_cpu", XiaoNrf54L15.cpuFrequencyFromToolsHz());
  printHz("active-cpu", XiaoNrf54L15.cpuFrequencyHz());

#if TARGET_RUNTIME_CPU_HZ != 0UL
  Serial.print("runtime-set-request: ");
  Serial.println((uint32_t)TARGET_RUNTIME_CPU_HZ);
  if (XiaoNrf54L15.setCpuFrequencyHz((uint32_t)TARGET_RUNTIME_CPU_HZ)) {
    Serial.println("runtime-set: ok");
  } else {
    Serial.println("runtime-set: failed");
  }
  printHz("active-cpu", XiaoNrf54L15.cpuFrequencyHz());
#else
  Serial.println("runtime-set: disabled (TARGET_RUNTIME_CPU_HZ=0)");
#endif
}

void loop()
{
  printHz("active-cpu", XiaoNrf54L15.cpuFrequencyHz());
  delay(2000);
}
