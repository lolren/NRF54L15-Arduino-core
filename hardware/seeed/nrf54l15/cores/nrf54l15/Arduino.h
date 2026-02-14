#ifndef Arduino_h
#define Arduino_h

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef bool boolean;
typedef uint8_t byte;
typedef unsigned int word;
typedef uint8_t pin_size_t;

#define HIGH 0x1
#define LOW  0x0

#define INPUT 0x0
#define OUTPUT 0x1
#define INPUT_PULLUP 0x2
#define INPUT_PULLDOWN 0x3

#define LSBFIRST 0
#define MSBFIRST 1

#define CHANGE 0x1
#define FALLING 0x2
#define RISING 0x3

#define DEFAULT 1

#define DEC 10
#define HEX 16
#define OCT 8
#define BIN 2

#ifndef PI
#define PI 3.1415926535897932384626433832795
#endif
#define HALF_PI 1.5707963267948966192313216916398
#define TWO_PI 6.283185307179586476925286766559
#define DEG_TO_RAD 0.017453292519943295769236907684886
#define RAD_TO_DEG 57.295779513082320876798154814105

#ifndef abs
#define abs(x) ((x) > 0 ? (x) : -(x))
#endif
#define constrain(amt, low, high) ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))
#ifndef round
#define round(x) ((x) >= 0 ? (long)((x) + 0.5) : (long)((x) - 0.5))
#endif
#define radians(deg) ((deg) * DEG_TO_RAD)
#define degrees(rad) ((rad) * RAD_TO_DEG)
#define sq(x) ((x) * (x))

#define lowByte(w) ((uint8_t)((w) & 0xff))
#define highByte(w) ((uint8_t)((w) >> 8))

#define bitRead(value, bit) (((value) >> (bit)) & 0x01)
#define bitSet(value, bit) ((value) |= (1UL << (bit)))
#define bitClear(value, bit) ((value) &= ~(1UL << (bit)))
#define bitToggle(value, bit) ((value) ^= (1UL << (bit)))
#define bitWrite(value, bit, bitvalue) ((bitvalue) ? bitSet(value, bit) : bitClear(value, bit))

#define clockCyclesPerMicrosecond() (F_CPU / 1000000L)
#define clockCyclesToMicroseconds(a) ((a) / clockCyclesPerMicrosecond())
#define microsecondsToClockCycles(a) ((a) * clockCyclesPerMicrosecond())

#define F(str_literal) (str_literal)

#ifdef __cplusplus
extern "C" {
#endif

void init(void);
void initVariant(void);
void yield(void);

void pinMode(uint8_t pin, uint8_t mode);
void digitalWrite(uint8_t pin, uint8_t val);
int digitalRead(uint8_t pin);

int analogRead(uint8_t pin);
void analogReference(uint8_t mode);
void analogReadResolution(uint8_t bits);
void analogWriteResolution(uint8_t bits);
void analogWrite(uint8_t pin, int value);

unsigned long millis(void);
unsigned long micros(void);
void delay(unsigned long ms);
void delayMicroseconds(unsigned int us);

#ifdef __cplusplus
void tone(uint8_t pin, unsigned int frequency, unsigned long duration = 0);
#else
void tone(uint8_t pin, unsigned int frequency, unsigned long duration);
#endif
void noTone(uint8_t pin);

void shiftOut(uint8_t dataPin, uint8_t clockPin, uint8_t bitOrder, uint8_t value);
uint8_t shiftIn(uint8_t dataPin, uint8_t clockPin, uint8_t bitOrder);
unsigned long pulseIn(uint8_t pin, uint8_t state, unsigned long timeout);
unsigned long pulseInLong(uint8_t pin, uint8_t state, unsigned long timeout);

long arduinoRandom(long max);
long arduinoRandomRange(long min, long max);
void randomSeed(unsigned long seed);

void attachInterrupt(uint8_t pin, void (*userFunc)(void), int mode);
void detachInterrupt(uint8_t pin);
void interrupts(void);
void noInterrupts(void);

long map(long, long, long, long, long);

void setup(void);
void loop(void);

#ifdef __cplusplus
}
#endif

#include "pins_arduino.h"

#ifdef __cplusplus
inline long random(long max)
{
    return arduinoRandom(max);
}

inline long random(long min, long max)
{
    return arduinoRandomRange(min, max);
}

#include "WCharacter.h"
#include "WString.h"
#include "Print.h"
#include "Stream.h"
#include "HardwareSerial.h"
#include "XiaoNrf54L15.h"
#endif

#endif
