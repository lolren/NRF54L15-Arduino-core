#include "Arduino.h"

#include <generated/zephyr/autoconf.h>

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

extern const struct gpio_dt_spec g_pin_map[];
extern const size_t g_pin_map_size;

struct arduino_interrupt_slot {
    struct gpio_callback callback;
    void (*handler)(void);
    const struct device *port;
    bool attached;
};

static struct arduino_interrupt_slot g_interrupts[NUM_DIGITAL_PINS];
static bool g_irqs_locked;
static unsigned int g_irq_lock_key;

static void interruptHandler(const struct device *port, struct gpio_callback *cb, uint32_t pins)
{
    ARG_UNUSED(port);
    ARG_UNUSED(pins);

    struct arduino_interrupt_slot *slot = CONTAINER_OF(cb, struct arduino_interrupt_slot, callback);
    if (slot->handler != NULL) {
        slot->handler();
    }
}

static bool isValidPin(uint8_t pin)
{
    return pin < g_pin_map_size;
}

void pinMode(uint8_t pin, uint8_t mode)
{
    if (!isValidPin(pin)) {
        return;
    }

    const struct gpio_dt_spec *spec = &g_pin_map[pin];
    if (!device_is_ready(spec->port)) {
        return;
    }

    gpio_flags_t flags = GPIO_INPUT;

    switch (mode) {
    case INPUT:
        flags = GPIO_INPUT;
        break;
    case OUTPUT:
        flags = GPIO_OUTPUT;
        break;
    case INPUT_PULLUP:
        flags = GPIO_INPUT | GPIO_PULL_UP;
        break;
    case INPUT_PULLDOWN:
        flags = GPIO_INPUT | GPIO_PULL_DOWN;
        break;
    default:
        flags = GPIO_INPUT;
        break;
    }

    (void)gpio_pin_configure_dt(spec, flags);
}

void digitalWrite(uint8_t pin, uint8_t value)
{
    if (!isValidPin(pin)) {
        return;
    }

    const struct gpio_dt_spec *spec = &g_pin_map[pin];
    if (!device_is_ready(spec->port)) {
        return;
    }

    (void)gpio_pin_set_dt(spec, value ? 1 : 0);
}

int digitalRead(uint8_t pin)
{
    if (!isValidPin(pin)) {
        return LOW;
    }

    const struct gpio_dt_spec *spec = &g_pin_map[pin];
    if (!device_is_ready(spec->port)) {
        return LOW;
    }

    int value = gpio_pin_get_dt(spec);
    return (value > 0) ? HIGH : LOW;
}

void attachInterrupt(uint8_t pin, void (*userFunc)(void), int mode)
{
    if (!isValidPin(pin) || userFunc == NULL || pin >= NUM_DIGITAL_PINS) {
        return;
    }

    const struct gpio_dt_spec *spec = &g_pin_map[pin];
    if (!device_is_ready(spec->port)) {
        return;
    }

    detachInterrupt(pin);

    pinMode(pin, INPUT);

    gpio_flags_t irq_flags = GPIO_INT_EDGE_BOTH;
    switch (mode) {
    case RISING:
        irq_flags = GPIO_INT_EDGE_TO_ACTIVE;
        break;
    case FALLING:
        irq_flags = GPIO_INT_EDGE_TO_INACTIVE;
        break;
    case CHANGE:
    default:
        irq_flags = GPIO_INT_EDGE_BOTH;
        break;
    }

    g_interrupts[pin].handler = userFunc;
    g_interrupts[pin].port = spec->port;
    g_interrupts[pin].attached = true;

    gpio_init_callback(&g_interrupts[pin].callback, interruptHandler, BIT(spec->pin));
    (void)gpio_add_callback(spec->port, &g_interrupts[pin].callback);
    (void)gpio_pin_interrupt_configure_dt(spec, irq_flags);
}

void detachInterrupt(uint8_t pin)
{
    if (!isValidPin(pin) || pin >= NUM_DIGITAL_PINS) {
        return;
    }

    if (!g_interrupts[pin].attached) {
        return;
    }

    const struct gpio_dt_spec *spec = &g_pin_map[pin];
    (void)gpio_pin_interrupt_configure_dt(spec, GPIO_INT_DISABLE);

    if (g_interrupts[pin].port != NULL) {
        gpio_remove_callback(g_interrupts[pin].port, &g_interrupts[pin].callback);
    }

    g_interrupts[pin].handler = NULL;
    g_interrupts[pin].port = NULL;
    g_interrupts[pin].attached = false;
}

void noInterrupts(void)
{
    if (!g_irqs_locked) {
        g_irq_lock_key = irq_lock();
        g_irqs_locked = true;
    }
}

void interrupts(void)
{
    if (g_irqs_locked) {
        irq_unlock(g_irq_lock_key);
        g_irqs_locked = false;
    }
}

void shiftOut(uint8_t dataPin, uint8_t clockPin, uint8_t bitOrder, uint8_t value)
{
    for (uint8_t i = 0; i < 8; ++i) {
        uint8_t bit_index = (bitOrder == LSBFIRST) ? i : (7 - i);
        digitalWrite(dataPin, (value >> bit_index) & 0x01);
        digitalWrite(clockPin, HIGH);
        digitalWrite(clockPin, LOW);
    }
}

uint8_t shiftIn(uint8_t dataPin, uint8_t clockPin, uint8_t bitOrder)
{
    uint8_t value = 0;

    for (uint8_t i = 0; i < 8; ++i) {
        digitalWrite(clockPin, HIGH);
        uint8_t bit = (digitalRead(dataPin) == HIGH) ? 1 : 0;
        uint8_t bit_index = (bitOrder == LSBFIRST) ? i : (7 - i);
        value |= (bit << bit_index);
        digitalWrite(clockPin, LOW);
    }

    return value;
}

unsigned long pulseIn(uint8_t pin, uint8_t state, unsigned long timeout)
{
    unsigned long start = micros();

    while (digitalRead(pin) == state) {
        if ((micros() - start) >= timeout) {
            return 0;
        }
    }

    while (digitalRead(pin) != state) {
        if ((micros() - start) >= timeout) {
            return 0;
        }
    }

    unsigned long pulse_start = micros();

    while (digitalRead(pin) == state) {
        if ((micros() - start) >= timeout) {
            return 0;
        }
    }

    return micros() - pulse_start;
}

unsigned long pulseInLong(uint8_t pin, uint8_t state, unsigned long timeout)
{
    return pulseIn(pin, state, timeout);
}
