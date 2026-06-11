/*
 * XIAO nRF54LM20A pin definitions for clean bare-metal core.
 *
 * Pin mapping follows the Seeed Studio XIAO nRF54LM20A pin map and schematic.
 *
 * Pin Port mapping:
 *   P0: GPIO 0.00-0.09   (button, NFC, GRTC, peripherals)
 *   P1: GPIO 1.00-1.31   (D0-D10, LED, ADC, PMIC I2C)
 *   P3: GPIO 3.00-3.12   (D11-D18, D25-D27, IMU CS)
 */

#ifndef Pins_Arduino_h
#define Pins_Arduino_h

#include <stdint.h>
#include <nrf54lm20b.h>

#define NUM_DIGITAL_PINS 28
#define NUM_ANALOG_INPUTS 5   // A0-A3 (D0-D3) + A7 (D4)

// ─── XIAO Connector Pins (D0–D27) ─────────────────────────────
#define PIN_D0  (0)
#define PIN_D1  (1)
#define PIN_D2  (2)
#define PIN_D3  (3)
#define PIN_D4  (4)
#define PIN_D5  (5)
#define PIN_D6  (6)
#define PIN_D7  (7)
#define PIN_D8  (8)
#define PIN_D9  (9)
#define PIN_D10 (10)
#define PIN_D11 (11)
#define PIN_D12 (12)
#define PIN_D13 (13)
#define PIN_D14 (14)
#define PIN_D15 (15)
#define PIN_D16 (16)
#define PIN_D17 (17)
#define PIN_D18 (18)
#define PIN_D19 (19)
#define PIN_D20 (20)
#define PIN_D21 (21)
#define PIN_D22 (22)
#define PIN_D23 (23)
#define PIN_D24 (24)
#define PIN_D25 (25)
#define PIN_D26 (26)
#define PIN_D27 (27)

// ─── RGB LED (active-low, common-anode) ───────────────────────
// nRF54LM20A: Red=P1.22, Blue=P1.23, Green=P1.24
#define PIN_LED_RED   (28)
#define PIN_LED_BLUE  (29)   // ⚠️ Not an XIAO connector pin
#define PIN_LED_GREEN (30)   // ⚠️ Not an XIAO connector pin

// Default builtin LED = Red (for backward compatibility)
#define PIN_LED_BUILTIN PIN_LED_RED
#define LED_BUILTIN     PIN_LED_RED
#define LED_RED         PIN_LED_RED
#define LED_GREEN       PIN_LED_GREEN
#define LED_BLUE        PIN_LED_BLUE
#define LED_STATE_ON    LOW            // active-low RGB LED

// ─── User Button (active-low, pull-up) ────────────────────────
#define PIN_BUTTON (31)

// ─── Analog Pins ──────────────────────────────────────────────
#define PIN_A0 PIN_D0
#define PIN_A1 PIN_D1
#define PIN_A2 PIN_D2
#define PIN_A3 PIN_D3
#define PIN_A4 PIN_D4   // D4 also serves as AIN7

enum {
    A0 = PIN_A0,
    A1 = PIN_A1,
    A2 = PIN_A2,
    A3 = PIN_A3,
    A4 = PIN_A4,
};

// ─── Communication Interfaces ─────────────────────────────────
// I2C: Wire (XIAO standard header pair)
#define PIN_WIRE_SDA  (PIN_D4)   // P1.03
#define PIN_WIRE_SCL  (PIN_D5)   // P1.07

// Wire1 (second I2C) — same pins as Wire on LM20A for compatibility
// LM20A has one I2C pair on the XIAO connector header.
#define PIN_WIRE1_SDA PIN_WIRE_SDA
#define PIN_WIRE1_SCL PIN_WIRE_SCL

// UART (Serial1 hardware)
#define PIN_SERIAL_TX  (PIN_D6)  // P1.08
#define PIN_SERIAL_RX  (PIN_D7)  // P1.09
#define PIN_SERIAL1_TX PIN_SERIAL_TX
#define PIN_SERIAL1_RX PIN_SERIAL_RX

// SPI
#define PIN_SPI_MOSI (PIN_D10)   // P1.06
#define PIN_SPI_MISO (PIN_D9)    // P1.05
#define PIN_SPI_SCK  (PIN_D8)    // P1.04
#define PIN_SPI_SS   (PIN_D2)    // P1.30 (same as D2, no dedicated SS)

// ─── Internal Pins (not on XIAO connector) ────────────────────
// PMIC I2C (nPM1300): SDA=P1.18, SCL=P1.17
#define PIN_PMIC_SDA (32)       // P1.18
#define PIN_PMIC_SCL (33)       // P1.17
// No RF switch on LM20A - antenna path is fixed by board hardware.

// CDC USB Serial — UART20 on P1.11/P1.10 (connected to SAMD11 debug probe)
#define PIN_CDC_TX   (34)   // P1.11
#define PIN_CDC_RX   (35)   // P1.10
#define CDC_TX       PIN_CDC_TX
#define CDC_RX       PIN_CDC_RX

// Compatibility aliases for shared core code expecting SAMD11 bridge pins.
// Names are from the bridge side: MCU TX -> SAMD11_RX, SAMD11_TX -> MCU RX.
#define PIN_SAMD11_RX PIN_CDC_TX       // P1.11
#define PIN_SAMD11_TX PIN_CDC_RX       // P1.10

// ─── Port Register Helpers ────────────────────────────────────
typedef volatile uint32_t PortReg;
typedef uint32_t PortMask;

static inline bool pinToPortPin(uint8_t pin, uint8_t* port, uint8_t* pinInPort)
{
    if (port == 0 || pinInPort == 0) {
        return false;
    }

    switch (pin) {
        // D0-D10 on Port 1
        case PIN_D0:  *port = 1; *pinInPort = 0;  return true;   // P1.00
        case PIN_D1:  *port = 1; *pinInPort = 31; return true;   // P1.31
        case PIN_D2:  *port = 1; *pinInPort = 30; return true;   // P1.30
        case PIN_D3:  *port = 1; *pinInPort = 29; return true;   // P1.29
        case PIN_D4:  *port = 1; *pinInPort = 3;  return true;   // P1.03
        case PIN_D5:  *port = 1; *pinInPort = 7;  return true;   // P1.07
        case PIN_D6:  *port = 1; *pinInPort = 8;  return true;   // P1.08
        case PIN_D7:  *port = 1; *pinInPort = 9;  return true;   // P1.09
        case PIN_D8:  *port = 1; *pinInPort = 4;  return true;   // P1.04
        case PIN_D9:  *port = 1; *pinInPort = 5;  return true;   // P1.05
        case PIN_D10: *port = 1; *pinInPort = 6;  return true;   // P1.06

        // D11-D18 on Port 3
        case PIN_D11: *port = 3; *pinInPort = 0;  return true;   // P3.00
        case PIN_D12: *port = 3; *pinInPort = 1;  return true;   // P3.01
        case PIN_D13: *port = 3; *pinInPort = 2;  return true;   // P3.02
        case PIN_D14: *port = 3; *pinInPort = 3;  return true;   // P3.03
        case PIN_D15: *port = 3; *pinInPort = 4;  return true;   // P3.04
        case PIN_D16: *port = 3; *pinInPort = 5;  return true;   // P3.05
        case PIN_D17: *port = 3; *pinInPort = 6;  return true;   // P3.06
        case PIN_D18: *port = 3; *pinInPort = 7;  return true;   // P3.07

        // D19-D24 on Port 0
        case PIN_D19: *port = 0; *pinInPort = 0;  return true;   // P0.00
        case PIN_D20: *port = 0; *pinInPort = 1;  return true;   // P0.01
        case PIN_D21: *port = 0; *pinInPort = 2;  return true;   // P0.02
        case PIN_D22: *port = 0; *pinInPort = 3;  return true;   // P0.03
        case PIN_D23: *port = 0; *pinInPort = 4;  return true;   // P0.04
        case PIN_D24: *port = 0; *pinInPort = 5;  return true;   // P0.05

        // D25-D27 on Port 3 (continued)
        case PIN_D25: *port = 3; *pinInPort = 9;  return true;   // P3.09
        case PIN_D26: *port = 3; *pinInPort = 10; return true;   // P3.10
        case PIN_D27: *port = 3; *pinInPort = 11; return true;   // P3.11

        // RGB LED on Port 1
        case PIN_LED_RED:   *port = 1; *pinInPort = 22; return true;  // P1.22
        case PIN_LED_BLUE:  *port = 1; *pinInPort = 23; return true;  // P1.23
        case PIN_LED_GREEN: *port = 1; *pinInPort = 24; return true;  // P1.24

        // Button on Port 0
        case PIN_BUTTON: *port = 0; *pinInPort = 9; return true;  // P0.09

        // PMIC I2C on Port 1
        case PIN_PMIC_SDA: *port = 1; *pinInPort = 18; return true;
        case PIN_PMIC_SCL: *port = 1; *pinInPort = 17; return true;

        // SAMD11 USB serial bridge on Port 1
        case PIN_CDC_TX: *port = 1; *pinInPort = 11; return true;
        case PIN_CDC_RX: *port = 1; *pinInPort = 10; return true;

        default: return false;
    }
}

static inline int8_t pinToSaadcChannel(uint8_t pin)
{
    switch (pin) {
        case PIN_A0: return 0;   // D0 = P1.00 = AIN0
        case PIN_A1: return 1;   // D1 = P1.31 = AIN1
        case PIN_A2: return 2;   // D2 = P1.30 = AIN2
        case PIN_A3: return 3;   // D3 = P1.29 = AIN3
        case PIN_A4: return 7;   // D4 = P1.03 = AIN7
        default: return -1;
    }
}

static inline uint8_t digitalPinToPort(uint8_t pin)
{
    uint8_t port = 0;
    uint8_t pinInPort = 0;
    return pinToPortPin(pin, &port, &pinInPort) ? port : 0xFF;
}

static inline uint32_t digitalPinToBitMask(uint8_t pin)
{
    uint8_t port = 0;
    uint8_t pinInPort = 0;
    (void)port;
    return pinToPortPin(pin, &port, &pinInPort) ? (1UL << pinInPort) : 0UL;
}

static inline volatile uint32_t* portOutputRegister(uint8_t port)
{
    switch (port) {
        case 0: return &NRF_P0->OUT;
        case 1: return &NRF_P1->OUT;
        case 3: return &NRF_P3->OUT;
        default: return (volatile uint32_t*)0;
    }
}

static inline volatile uint32_t* portInputRegister(uint8_t port)
{
    switch (port) {
        case 0: return (volatile uint32_t*)&NRF_P0->IN;
        case 1: return (volatile uint32_t*)&NRF_P1->IN;
        case 3: return (volatile uint32_t*)&NRF_P3->IN;
        default: return (volatile uint32_t*)0;
    }
}

static inline volatile uint32_t* portModeRegister(uint8_t port)
{
    switch (port) {
        case 0: return &NRF_P0->DIR;
        case 1: return &NRF_P1->DIR;
        case 3: return &NRF_P3->DIR;
        default: return (volatile uint32_t*)0;
    }
}

// PWM-capable pins: D0-D10 (Port 1 pins are PWM-capable)
#define digitalPinHasPWM(p) ((p) <= PIN_D10)

#ifndef NOT_AN_INTERRUPT
#define NOT_AN_INTERRUPT 0xFF
#endif

static inline int digitalPinToInterrupt(uint8_t pin)
{
    uint8_t port = 0;
    uint8_t pinInPort = 0;
    // Port 3 pins do not support GPIO interrupts on nRF54L series
    if (!pinToPortPin(pin, &port, &pinInPort) || port == 3U) {
        return NOT_AN_INTERRUPT;
    }
    return pin;
}

static inline uint8_t analogInputToDigitalPin(uint8_t p)
{
    switch (p) {
        case 0: return PIN_A0;
        case 1: return PIN_A1;
        case 2: return PIN_A2;
        case 3: return PIN_A3;
        case 7: return PIN_A4;
        default: return 0xFF;
    }
}

// ─── Static Pin References ────────────────────────────────────
static const uint8_t SDA  = PIN_WIRE_SDA;
static const uint8_t SCL  = PIN_WIRE_SCL;
static const uint8_t SDA1 = PIN_WIRE1_SDA;
static const uint8_t SCL1 = PIN_WIRE1_SCL;
static const uint8_t MOSI = PIN_SPI_MOSI;
static const uint8_t MISO = PIN_SPI_MISO;
static const uint8_t SCK  = PIN_SPI_SCK;
static const uint8_t SS   = PIN_SPI_SS;

// ─── Serial Port Aliases ──────────────────────────────────────
#define SERIAL_PORT_MONITOR       Serial
#define SERIAL_PORT_USBVIRTUAL    Serial
#define SERIAL_PORT_HARDWARE      Serial1
#define SERIAL_PORT_HARDWARE1     Serial1

#define HAVE_HWSERIAL1

#endif
