/*
  AntennaControl

  Selects the XIAO nRF54L15 RF path at runtime.
  Open Serial Monitor at 115200 and send:
    c -> on-board ceramic antenna
    e -> external U.FL antenna
*/

#include <XiaoNrf54L15.h>

void printSelection()
{
  if (XiaoNrf54L15.getAntenna() == XiaoNrf54L15Class::EXTERNAL) {
    Serial.println("Antenna: external U.FL");
  } else {
    Serial.println("Antenna: ceramic on-board");
  }
}

void setup()
{
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("Antenna control ready. Send 'c' or 'e'.");
  printSelection();
}

void loop()
{
  while (Serial.available() > 0) {
    int ch = Serial.read();

    if (ch == 'c' || ch == 'C') {
      if (XiaoNrf54L15.useCeramicAntenna()) {
        Serial.println("Switched to ceramic antenna");
      } else {
        Serial.println("Failed to switch antenna");
      }
      printSelection();
    } else if (ch == 'e' || ch == 'E') {
      if (XiaoNrf54L15.useExternalAntenna()) {
        Serial.println("Switched to external U.FL antenna");
      } else {
        Serial.println("Failed to switch antenna");
      }
      printSelection();
    }
  }
}
