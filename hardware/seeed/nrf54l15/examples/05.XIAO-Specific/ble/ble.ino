#include <Arduino.h>
#include <Bluetooth.h>
#include <XiaoNrf54L15.h>

#if defined(ARDUINO_XIAO_NRF54L15_RADIO_802154_ONLY)

void setup() {
  Serial.begin(115200);
  delay(1200);
  Serial.println("BLE sample disabled in 802.15.4-only profile.");
  Serial.println("Select BLE Only or BLE + 802.15.4 in Tools -> Radio Profile.");
}

void loop() {
  delay(1000);
}

#else

static bool g_advRunning = false;

static void showMenu() {
  Serial.println();
  Serial.println("Commands:");
  Serial.println("  a - start advertising");
  Serial.println("  x - stop advertising");
  Serial.println("  s - run one 4s BLE scan");
  Serial.println();
}

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);

  Serial.begin(115200);
  delay(1200);

  (void)XiaoNrf54L15.useCeramicAntenna();

  Serial.println("XIAO nRF54L15 BLE sample");
  if (!BLE.begin("XIAO-nRF54L15-BLE")) {
    Serial.print("BLE init failed, err=");
    Serial.println(BLE.lastError());
  } else {
    Serial.println("BLE init ok");
  }

  showMenu();
}

void loop() {
  if (Serial.available() > 0) {
    const char cmd = static_cast<char>(Serial.read());
    if (cmd == 'a') {
      g_advRunning = BLE.advertise();
      Serial.print("advertise: ");
      Serial.println(g_advRunning ? "ok" : "failed");
      if (!g_advRunning) {
        Serial.print("err=");
        Serial.println(BLE.lastError());
      }
    } else if (cmd == 'x') {
      BLE.stopAdvertising();
      g_advRunning = false;
      Serial.println("advertise: stopped");
    } else if (cmd == 's') {
      Serial.println("scan start (4s)...");
      if (BLE.scan(4000)) {
        Serial.print("found: ");
        Serial.print(BLE.name());
        Serial.print("  ");
        Serial.print(BLE.address());
        Serial.print("  RSSI=");
        Serial.println(BLE.rssi());
      } else {
        Serial.print("no result, err=");
        Serial.println(BLE.lastError());
      }
    }
  }

  digitalWrite(LED_BUILTIN, g_advRunning ? LOW : HIGH);
  delay(20);
}

#endif
