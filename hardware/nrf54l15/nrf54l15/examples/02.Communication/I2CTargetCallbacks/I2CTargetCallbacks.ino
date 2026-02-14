/*
 * I2C Target Callbacks Example
 *
 * Demonstrates Wire target/slave mode callbacks:
 * - Wire.begin(0x42)
 * - Wire.onReceive(...)
 * - Wire.onRequest(...)
 *
 * Test with another controller/master on the same I2C bus.
 */

#include <Wire.h>

static volatile uint8_t g_lastByte = 0x5A;
static volatile uint8_t g_bytesInLastWrite = 0;
static volatile bool g_writeEvent = false;

void onReceiveEvent(int count)
{
  g_bytesInLastWrite = static_cast<uint8_t>(count);
  while (Wire.available() > 0) {
    g_lastByte = static_cast<uint8_t>(Wire.read());
  }
  g_writeEvent = true;
}

void onRequestEvent()
{
  Wire.write(g_lastByte);
}

void setup()
{
  Serial.begin(115200);
  while (!Serial) {
    delay(10);
  }

  Wire.begin(0x42);
  Wire.onReceive(onReceiveEvent);
  Wire.onRequest(onRequestEvent);

  Serial.println("I2C target ready at 0x42");
  Serial.println("Write bytes from a controller; target echoes last byte on reads.");
}

void loop()
{
  if (g_writeEvent) {
    noInterrupts();
    const uint8_t last = g_lastByte;
    const uint8_t count = g_bytesInLastWrite;
    g_writeEvent = false;
    interrupts();

    Serial.print("rx count=");
    Serial.print(count);
    Serial.print(" last=0x");
    Serial.println(last, HEX);
  }

  delay(20);
}
