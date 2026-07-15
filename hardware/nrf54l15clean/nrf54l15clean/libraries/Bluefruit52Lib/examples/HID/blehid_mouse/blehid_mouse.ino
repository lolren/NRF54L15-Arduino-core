/*********************************************************************
 This is an example for our nRF52 based Bluefruit LE modules

 Pick one up today in the adafruit shop!

 Adafruit invests time and resources providing this open source code,
 please support Adafruit and open-source hardware by purchasing
 products from Adafruit!

 MIT license, check LICENSE for more information
 All text above, and the splash screen below must be included in
 any redistribution
*********************************************************************/
#include <bluefruit.h>

BLEDis bledis;
BLEBas blebas;
BLEHidAdafruit blehid;

#define MOVE_STEP    10
#define IDLE_REPORT_INTERVAL_MS 100UL

void connect_callback(uint16_t conn_handle);

void setup() 
{
  Serial.begin(115200);
  for (uint32_t start = millis(); !Serial && (millis() - start) < 1500; ) {
    delay(10);
  }

  Serial.println("Bluefruit52 HID Mouse Example");
  Serial.println("-----------------------------\n");
  Serial.println("Go to your phone's Bluetooth settings to pair your device");
  Serial.println("then open an application that accepts mouse input");
  Serial.println();

  Serial.println("Enter following characters");
  Serial.println("- 'WASD'  to move mouse (up, left, down, right)");
  Serial.println("- 'LRMBF' to press mouse button(s) (left, right, middle, backward, forward)");
  Serial.println("- 'X'     to release mouse button(s)");

  Bluefruit.begin();
  Bluefruit.setAppearance(BLE_APPEARANCE_HID_MOUSE);
  // A mouse has no trustworthy display or keyboard. Advertise the matching
  // NoInputNoOutput capability so hosts select LE Secure Connections Just
  // Works instead of a passkey interaction the device cannot confirm.
  Bluefruit.Security.setIOCaps(false, false, false);
#if defined(ARDUINO_NRF54LM20A) || defined(ARDUINO_NRF54LM20B)
  // LM20A needs extra event budget for software P-256 during secure pairing.
  Bluefruit.Periph.setConnInterval(24, 40); // 30-50 ms
#else
  // HID still feels instant at this range, and it gives Android enough event
  // budget for LE Secure Connections on hosts with stricter pairing timing.
  Bluefruit.Periph.setConnInterval(24, 40); // 30-50 ms
#endif
  Bluefruit.Periph.setConnectCallback(connect_callback);
  Bluefruit.setTxPower(4);    // Check bluefruit.h for supported values

  // Configure and Start Device Information Service
  bledis.setManufacturer("Adafruit Industries");
  bledis.setModel("Bluefruit Feather 52");
  bledis.begin();
  blebas.begin();
  blebas.write(100);

  // Match the compact Zephyr peripheral_hids profile used by Android and
  // Sony HID hosts. Applications needing the complete HOGP profile can opt in
  // with setZephyrCompatibleMouse(false).
  blehid.setZephyrCompatibleMouse(true);
  blehid.begin();

  // Set up and start advertising
  startAdv();
}

void startAdv(void)
{  
  // Advertising packet
  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  
  // Zephyr peripheral_hids advertises HIDS + BAS UUIDs and puts the name in
  // scan response. Keep this shape for strict Android host comparisons.
  Bluefruit.Advertising.addService(blehid, blebas);

  Bluefruit.ScanResponse.addName();
  
  /* Start Advertising
   * - Enable auto advertising if disconnected
   * - Interval:  fast mode = 20 ms, slow mode = 152.5 ms
   * - Timeout for fast mode is 30 seconds
   * - Start(timeout) with timeout = 0 will advertise forever (until connected)
   * 
   * For recommended advertising interval
   * https://developer.apple.com/library/content/qa/qa1931/_index.html   
   */
  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.setInterval(32, 244);    // in unit of 0.625 ms
  Bluefruit.Advertising.setFastTimeout(30);      // number of seconds in fast mode
  Bluefruit.Advertising.start(0);                // 0 = Don't stop advertising after n seconds  
}

void loop() 
{    
  static uint32_t lastIdleReportMs = 0;
  if (Bluefruit.connected() && Bluefruit.Security.isEncrypted(0U) &&
      blehid.mouseNotifyEnabled())
  {
    const uint32_t now = millis();
    if ((now - lastIdleReportMs) >= IDLE_REPORT_INTERVAL_MS)
    {
      lastIdleReportMs = now;
      blehid.mouseMove(0, 0);
    }
  }
  else
  {
    lastIdleReportMs = millis();
  }

  if (Serial.available())
  {
    char ch = (char) Serial.read();

    // convert to upper case
    ch = (char) toupper(ch);
    
    // echo
    Serial.println(ch);
  
    switch(ch)
    {
      // WASD to move the mouse
      case 'W':
        blehid.mouseMove(0, -MOVE_STEP);
      break;

      case 'A':
        blehid.mouseMove(-MOVE_STEP, 0);
      break;

      case 'S':
        blehid.mouseMove(0, MOVE_STEP);
      break;

      case 'D':
        blehid.mouseMove(MOVE_STEP, 0);
      break;

      // LRMBF for mouse button(s)
      case 'L':
        blehid.mouseButtonPress(MOUSE_BUTTON_LEFT);
      break;

      case 'R':
        blehid.mouseButtonPress(MOUSE_BUTTON_RIGHT);
      break;

      case 'M':
        blehid.mouseButtonPress(MOUSE_BUTTON_MIDDLE);
      break;

      case 'B':
        blehid.mouseButtonPress(MOUSE_BUTTON_BACKWARD);
      break;

      case 'F':
        // This key is not always supported by every OS
        blehid.mouseButtonPress(MOUSE_BUTTON_FORWARD);
      break;

      case 'X':
        // X to release all buttons
        blehid.mouseButtonRelease();
      break;

      default: break;
    }
  }
}

void connect_callback(uint16_t conn_handle)
{
  (void) conn_handle;
  Bluefruit.Security.requestPairing();
}

void set_protocol_mode(uint16_t conn_handle, uint8_t mode)
{
  (void) conn_handle;
  Serial.print("HID protocol mode: ");
  Serial.println(mode == BLE_HID_PROTOCOL_MODE_BOOT ? "Boot" : "Report");
}
