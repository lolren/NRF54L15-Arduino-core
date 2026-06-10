/*
 * XiaoRgbLed — RGB LED demo for XIAO nRF54L15 / nRF54LM20B
 *
 * Cycles through colors using PWM fading on the built-in RGB LED.
 *
 * XIAO nRF54L15:   single LED on P2.00 (active-low) — shows heartbeat
 * XIAO nRF54LM20B: RGB LED on P1.22(R) P1.23(B) P1.24(G) — active-low
 *
 * Both boards are auto-detected and the example adapts automatically.
 *
 * Hardware:
 *   - XIAO nRF54L15  / Sense
 *   - XIAO nRF54LM20B
 */

#if defined(ARDUINO_NRF54LM20B)
  // ─── LM20B: RGB LED ─────────────────────────────────────────
  const int LED_R = LED_RED;
  const int LED_G = LED_GREEN;
  const int LED_B = LED_BLUE;
  const int NUM_LEDS = 3;
  const int ledPins[3] = { LED_R, LED_G, LED_B };
  const char* ledNames[3] = { "Red", "Green", "Blue" };
#else
  // ─── L15: single LED — simulate RGB by blinking ─────────────
  const int LED_R = LED_BUILTIN;
  const int LED_G = LED_BUILTIN;
  const int LED_B = LED_BUILTIN;
  const int NUM_LEDS = 1;
  const int ledPins[1] = { LED_BUILTIN };
  const char* ledNames[1] = { "LED" };
#endif

void setup() {
#if defined(ARDUINO_NRF54LM20B)
  Serial.begin(115200);
  Serial.println("\n--- XIAO nRF54LM20B RGB LED Demo ---");
  Serial.println("Cycling: Red → Green → Blue → White → Fade");
#endif

  for (int i = 0; i < NUM_LEDS; i++) {
    pinMode(ledPins[i], OUTPUT);
    digitalWrite(ledPins[i], HIGH);  // Off (active-low)
  }
}

void setColor(int r, int g, int b) {
  // All values 0-255, 0 = full brightness (active-low)
#if defined(ARDUINO_NRF54LM20B)
  analogWrite(LED_R, 255 - r);
  analogWrite(LED_G, 255 - g);
  analogWrite(LED_B, 255 - b);
#else
  // L15: just blink proportionally
  int brightness = (r + g + b) / 3;
  digitalWrite(LED_BUILTIN, brightness > 128 ? HIGH : LOW);
#endif
}

void loop() {
  // ─── Phase 1: Solid colors ──────────────────────────────────
  setColor(255, 0, 0);    delay(800);   // Red
  setColor(0, 255, 0);    delay(800);   // Green
  setColor(0, 0, 255);    delay(800);   // Blue
  setColor(255, 255, 255); delay(800);  // White
  setColor(0, 0, 0);       delay(400);  // Off

  // ─── Phase 2: Rainbow fade ──────────────────────────────────
  for (int phase = 0; phase < 360; phase += 2) {
    int r = (phase < 120) ? 255 - (phase * 255 / 120) :
            (phase < 240) ? 0 :
            (phase - 240) * 255 / 120;
    int g = (phase < 120) ? phase * 255 / 120 :
            (phase < 240) ? 255 - ((phase - 120) * 255 / 120) :
            0;
    int b = (phase < 120) ? 0 :
            (phase < 240) ? (phase - 120) * 255 / 120 :
            255 - ((phase - 240) * 255 / 120);
    
    // Clamp
    if (r < 0) r = 0; if (r > 255) r = 255;
    if (g < 0) g = 0; if (g > 255) g = 255;
    if (b < 0) b = 0; if (b > 255) b = 255;
    
    setColor(r, g, b);
    delay(8);
  }
  
  // ─── Phase 3: All-off pause ─────────────────────────────────
  setColor(0, 0, 0);
  delay(500);
}
