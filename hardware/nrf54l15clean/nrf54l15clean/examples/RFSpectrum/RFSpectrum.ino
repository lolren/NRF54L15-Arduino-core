/*
 * RF Spectrum Analyzer — nRF54L 2.4 GHz band
 *
 * Sweeps 2400-2483 MHz using the built-in BLE 1M radio in RX mode.
 * Reads RSSI per MHz via RSSISAMPLE register (nRF54L RADIO peripheral).
 *
 * Output: 84-char line per sweep, digit 0-9 = signal strength.
 * Labels every 10 MHz at bottom.
 *
 * Radio event offsets (nRF54L differs from nRF52):
 *   EVENTS_READY    @ 0x200
 *   EVENTS_RXREADY  @ 0x208
 *   EVENTS_END      @ 0x218
 *   EVENTS_DISABLED @ 0x220
 *   RSSISAMPLE      @ 0x718
 *   (No EVENTS_RSSIEND — read RSSISAMPLE immediately after RSSISTART)
 *   STOP does not fire EVENTS_END reliably — use DISABLE instead
 */
#include <Arduino.h>

volatile uint32_t* R = (volatile uint32_t*)0x5008A000UL;

static int8_t rssi_sample(int mhz) {
    // DISABLE
    R[0x010/4] = 1;
    for (volatile int i = 0; i < 10000 && !R[0x220/4]; i++);
    R[0x220/4] = 0;

    // Configure: BLE 1M, frequency
    R[0x510/4] = 3;           // MODE = BLE 1M
    R[0x650/4] = 0;           // MODECNF0
    R[0x708/4] = mhz - 2400;   // FREQUENCY
    R[0x200/4] = 0;            // SHORTS = 0

    // RXEN
    R[0x004/4] = 1;
    for (volatile int i = 0; i < 10000 && !R[0x208/4]; i++);
    R[0x208/4] = 0;

    // START
    R[0x008/4] = 1;
    for (volatile int i = 0; i < 1000; i++) __asm__("nop");

    // RSSI — trigger and read immediately
    R[0x014/4] = 1;  // RSSISTART
    for (volatile int i = 0; i < 1000; i++) __asm__("nop");
    int8_t r = (int8_t)R[0x718/4];  // RSSISAMPLE

    // DISABLE (STOP doesn't fire EVENTS_END on nRF54L)
    R[0x010/4] = 1;
    for (volatile int i = 0; i < 10000 && !R[0x220/4]; i++);
    R[0x220/4] = 0;

    return r;
}

void setup() {
    Serial.begin(115200);
    delay(250);
}

void loop() {
    for (int f = 2400; f <= 2483; f++) {
        int8_t v = rssi_sample(f);
        // Map ~63-73 range to 0-9
        int bar = constrain(map(v, 63, 73, 0, 9), 0, 9);
        Serial.print(bar);
    }
    Serial.print("  [");
    for (int f = 2400; f <= 2483; f += 10) {
        Serial.print(f);
        Serial.print(" ");
    }
    Serial.println("] MHz");
}
