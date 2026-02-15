#include <Arduino.h>
#include <HPFMSPI.h>

void setup() {
  Serial.begin(115200);
  delay(1200);

  Serial.println("XIAO nRF54L15 HPF MSPI diagnostic");
#if !defined(ARDUINO_XIAO_NRF54L15_HPF_MSPI)
  Serial.println("HPF MSPI is disabled in board settings.");
  Serial.println("Enable Tools -> HPF MSPI -> Experimental Enable, then rebuild.");
#endif

  Serial.println(HPFMSPI.info());

  if (!HPFMSPI.begin()) {
    Serial.print("HPFMSPI begin failed, err=");
    Serial.println(HPFMSPI.lastError());
    return;
  }

  const int ch0 = HPFMSPI.channelStatus(0);
  Serial.print("channel 0 status=");
  Serial.println(ch0);

  Serial.print("max frequency (Hz)=");
  Serial.println(HPFMSPI.maxFrequencyHz());
}

void loop() {
  delay(2000);
}
