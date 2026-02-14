/*
 * NeoPixel Data-Line Sanity Test for XIAO nRF54L15
 *
 * Sends a simple WS2812-style frame on D11 (common RGB LED data pin on XIAO boards)
 * without external libraries, and also blinks LED_BUILTIN so board activity is visible.
 */

static const uint8_t kNeoPixelDataPin = PIN_D11;

static void sendBit(bool one)
{
  // Coarse timing for a basic bring-up test.
  digitalWrite(kNeoPixelDataPin, HIGH);
  delayMicroseconds(one ? 1 : 0);
  digitalWrite(kNeoPixelDataPin, LOW);
  delayMicroseconds(one ? 0 : 1);
}

static void sendByte(uint8_t value)
{
  for (int8_t bit = 7; bit >= 0; --bit) {
    sendBit((value >> bit) & 0x01U);
  }
}

static void showColor(uint8_t r, uint8_t g, uint8_t b)
{
  noInterrupts();
  sendByte(g);
  sendByte(r);
  sendByte(b);
  interrupts();

  // Latch/reset period for WS2812 family.
  delayMicroseconds(80);
}

void setup()
{
  pinMode(kNeoPixelDataPin, OUTPUT);
  digitalWrite(kNeoPixelDataPin, LOW);

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH); // Active-low.
}

void loop()
{
  digitalWrite(LED_BUILTIN, LOW);
  showColor(32, 0, 0); // red
  delay(500);

  showColor(0, 32, 0); // green
  delay(500);

  showColor(0, 0, 32); // blue
  delay(500);

  showColor(0, 0, 0); // off
  digitalWrite(LED_BUILTIN, HIGH);
  delay(500);
}
