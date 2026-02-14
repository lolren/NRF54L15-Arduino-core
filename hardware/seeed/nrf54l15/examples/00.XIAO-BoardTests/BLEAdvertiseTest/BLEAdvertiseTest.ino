#include <Arduino.h>
#include <XiaoNrf54L15.h>

#if defined(ARDUINO_XIAO_NRF54L15_RADIO_802154_ONLY)

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  Serial.begin(115200);
  delay(1000);
  Serial.println("BLEAdvertiseTest: BLE is disabled by Tools -> Radio Profile (802.15.4 only).");
  Serial.println("Select BLE Only or BLE + 802.15.4 to run this sketch.");
}

void loop() {
  digitalWrite(LED_BUILTIN, LOW);
  delay(100);
  digitalWrite(LED_BUILTIN, HIGH);
  delay(900);
}

#else

#include <Bluetooth.h>

static bool g_bleReady = false;
static bool g_advRunning = false;
static unsigned long g_lastRetryMs = 0;

static const char *profileToText(XiaoNrf54L15Class::RadioProfile profile)
{
  switch (profile) {
    case XiaoNrf54L15Class::RADIO_BLE_ONLY: return "BLE only";
    case XiaoNrf54L15Class::RADIO_DUAL: return "BLE + 802.15.4";
    case XiaoNrf54L15Class::RADIO_802154_ONLY: return "802.15.4 only";
    default: return "unknown";
  }
}

static void tryStartAdvertising() {
  g_advRunning = BLE.advertise();
  if (g_advRunning) {
    Serial.println("BLE advertising started");
  } else {
    Serial.print("BLE advertising failed, err=");
    Serial.println(BLE.lastError());
  }
}

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);

  Serial.begin(115200);
  delay(1000);

  (void)XiaoNrf54L15.useCeramicAntenna();

  Serial.println("XIAO nRF54L15 BLE advertise test");
  Serial.print("Radio profile: ");
  Serial.println(profileToText(XiaoNrf54L15.getRadioProfile()));
  Serial.print("BLE TX power (build): ");
  Serial.print(XiaoNrf54L15.getBtTxPowerDbm());
  Serial.println(" dBm");
  Serial.print("Build antenna: ");
  Serial.println(XiaoNrf54L15.isExternalAntennaBuild() ? "External U.FL" : "Ceramic");

  g_bleReady = BLE.begin("XIAO-nRF54L15");
  if (!g_bleReady) {
    Serial.println("BLE init failed");
    Serial.print("err=");
    Serial.println(BLE.lastError());
    return;
  }

  tryStartAdvertising();
}

void loop() {
  if (g_bleReady && !g_advRunning) {
    const unsigned long now = millis();
    if (now - g_lastRetryMs >= 2000UL) {
      g_lastRetryMs = now;
      tryStartAdvertising();
    }
  }

  digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
  delay(g_advRunning ? 200 : 800);
}

#endif

