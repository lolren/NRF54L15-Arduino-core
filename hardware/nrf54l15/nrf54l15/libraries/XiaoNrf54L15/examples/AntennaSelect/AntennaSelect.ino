#include <XiaoNrf54L15.h>

void printState()
{
  Serial.print("Radio profile: ");
  Serial.println(XiaoNrf54L15.radioProfileName());
  Serial.print("Antenna: ");
  Serial.println(XiaoNrf54L15.usingExternalAntenna() ? "External U.FL" : "On-board ceramic");
}

void setup()
{
  Serial.begin(115200);
  while (!Serial) {
    delay(10);
  }

  Serial.println("XIAO nRF54L15 Antenna Control");
  Serial.println("Type 'c' for ceramic, 'e' for external antenna.");
  printState();
}

void loop()
{
  if (Serial.available()) {
    int ch = Serial.read();
    if (ch == 'c' || ch == 'C') {
      XiaoNrf54L15.setAntenna(XIAO_ANTENNA_CERAMIC);
      printState();
    } else if (ch == 'e' || ch == 'E') {
      XiaoNrf54L15.setAntenna(XIAO_ANTENNA_EXTERNAL);
      printState();
    }
  }
}
