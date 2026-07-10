#include "Arduino.h"

#include "cmsis.h"
#include <nrf54l15.h>

typedef struct {
    uint8_t port;
    uint8_t pin;
    uint8_t valid;
} pin_desc_t;

static volatile uint32_t g_irq_nest = 0;
static uint32_t g_irq_saved_primask = 0U;
void analogWriteDisable(uint8_t pin) __attribute__((weak));

#define CORE_GPIOTE20_CHANNEL_COUNT      8U
#define CORE_GPIOTE30_CHANNEL_COUNT      4U
#define CORE_GPIOTE_EVENTS_IN            0x100UL
#define CORE_GPIOTE_INTENSET0            0x304UL
#define CORE_GPIOTE_INTENCLR0            0x308UL
#define CORE_GPIOTE_CONFIG               0x510UL
#define CORE_GPIOTE_EVENTS_PORT_NS       0x140UL
#define CORE_GPIOTE_EVENTS_PORT_S        0x144UL

#define CORE_GPIOTE_CONFIG_MODE_Pos      0U
#define CORE_GPIOTE_CONFIG_PSEL_Pos      4U
#define CORE_GPIOTE_CONFIG_PORT_Pos      9U
#define CORE_GPIOTE_CONFIG_POLARITY_Pos  16U

#define CORE_GPIOTE_CONFIG_MODE_DISABLED 0U
#define CORE_GPIOTE_CONFIG_MODE_EVENT    1U
#define CORE_GPIOTE_POLARITY_LOTOHI      1U
#define CORE_GPIOTE_POLARITY_HITOLO      2U
#define CORE_GPIOTE_POLARITY_TOGGLE      3U

#ifdef NRF_TRUSTZONE_NONSECURE
#define CORE_GPIOTE20_BASE               0x400DA000UL
#define CORE_GPIOTE30_BASE               0x4010C000UL
#define CORE_GPIOTE_PORT_EVENT_OFFSET    CORE_GPIOTE_EVENTS_PORT_NS
#define CORE_GPIOTE_PORT_INT_MASK        (1UL << 16U)
#else
#define CORE_GPIOTE20_BASE               0x500DA000UL
#define CORE_GPIOTE30_BASE               0x5010C000UL
#define CORE_GPIOTE_PORT_EVENT_OFFSET    CORE_GPIOTE_EVENTS_PORT_S
#define CORE_GPIOTE_PORT_INT_MASK        (1UL << 17U)
#endif

#define PIN_TO_CHANNEL_UNUSED       0xFFU
#define IRQ_PIN_MAP_SIZE            32U

typedef struct {
    uint8_t in_use;
    uint8_t pin;
    uint8_t port;
    uint8_t level_low;
    void (*callback)(void);
} irq_channel_t;

static irq_channel_t g_irq_channels20[CORE_GPIOTE20_CHANNEL_COUNT];
static irq_channel_t g_irq_channels30[CORE_GPIOTE30_CHANNEL_COUNT];
static uint8_t g_irq_pin_to_channel[IRQ_PIN_MAP_SIZE];
static uint8_t g_task_channels_in_use[CORE_GPIOTE20_CHANNEL_COUNT];
static uint8_t g_irq_state_initialized = 0U;

static pin_desc_t resolve_pin(uint8_t pin)
{
    uint8_t port = 0U;
    uint8_t pinInPort = 0U;
    if (pinToPortPin(pin, &port, &pinInPort)) {
        return (pin_desc_t){port, pinInPort, 1U};
    }
    return (pin_desc_t){0U, 0U, 0U};
}

static NRF_GPIO_Type* gpio_for_port(uint8_t port)
{
    switch (port) {
        case 0: return NRF_P0;
        case 1: return NRF_P1;
        case 2: return NRF_P2;
        default: return (NRF_GPIO_Type*)0;
    }
}

static void irq_state_init_once(void)
{
    if (g_irq_state_initialized != 0U) {
        return;
    }

    for (uint8_t i = 0; i < IRQ_PIN_MAP_SIZE; ++i) {
        g_irq_pin_to_channel[i] = PIN_TO_CHANNEL_UNUSED;
    }
    for (uint8_t ch = 0; ch < CORE_GPIOTE20_CHANNEL_COUNT; ++ch) {
        g_irq_channels20[ch].in_use = 0U;
        g_irq_channels20[ch].pin = 0xFFU;
        g_irq_channels20[ch].port = 0xFFU;
        g_irq_channels20[ch].level_low = 0U;
        g_irq_channels20[ch].callback = 0;
        g_task_channels_in_use[ch] = 0U;
    }
    for (uint8_t ch = 0; ch < CORE_GPIOTE30_CHANNEL_COUNT; ++ch) {
        g_irq_channels30[ch].in_use = 0U;
        g_irq_channels30[ch].pin = 0xFFU;
        g_irq_channels30[ch].port = 0xFFU;
        g_irq_channels30[ch].level_low = 0U;
        g_irq_channels30[ch].callback = 0;
    }
    g_irq_state_initialized = 1U;
}

static inline volatile uint32_t* gpiote_regptr(uintptr_t base, uintptr_t off)
{
    return (volatile uint32_t*)(base + off);
}

static uintptr_t gpiote_base_for_port(uint8_t port)
{
    if (port == 0U) {
        return (uintptr_t)CORE_GPIOTE30_BASE;
    }
    if (port == 1U) {
        return (uintptr_t)CORE_GPIOTE20_BASE;
    }
    return 0U;
}

static irq_channel_t* irq_channels_for_base(uintptr_t base)
{
    return (base == (uintptr_t)CORE_GPIOTE30_BASE)
               ? g_irq_channels30 : g_irq_channels20;
}

static uint8_t gpiote_channel_count(uintptr_t base)
{
    return (base == (uintptr_t)CORE_GPIOTE30_BASE)
               ? CORE_GPIOTE30_CHANNEL_COUNT : CORE_GPIOTE20_CHANNEL_COUNT;
}

static uint32_t polarity_from_mode(int mode)
{
    if (mode == RISING) {
        return CORE_GPIOTE_POLARITY_LOTOHI;
    }
    if (mode == FALLING) {
        return CORE_GPIOTE_POLARITY_HITOLO;
    }
    return CORE_GPIOTE_POLARITY_TOGGLE;
}

static int8_t find_channel_for_pin(uint8_t pin, uintptr_t base)
{
    irq_channel_t* channels = irq_channels_for_base(base);
    const uint8_t channel_count = gpiote_channel_count(base);
    if (pin < IRQ_PIN_MAP_SIZE) {
        uint8_t mapped = g_irq_pin_to_channel[pin];
        if (mapped < channel_count && channels[mapped].in_use != 0U &&
            channels[mapped].pin == pin) {
            return (int8_t)mapped;
        }
    }

    for (uint8_t ch = 0; ch < channel_count; ++ch) {
        if (channels[ch].in_use != 0U && channels[ch].pin == pin) {
            return (int8_t)ch;
        }
    }

    return -1;
}

static int8_t alloc_channel(uintptr_t base)
{
    irq_channel_t* channels = irq_channels_for_base(base);
    const uint8_t channel_count = gpiote_channel_count(base);
    for (uint8_t ch = 0; ch < channel_count; ++ch) {
        const uint8_t task_in_use =
            (base == (uintptr_t)CORE_GPIOTE20_BASE) ? g_task_channels_in_use[ch] : 0U;
        if (channels[ch].in_use == 0U && task_in_use == 0U) {
            return (int8_t)ch;
        }
    }
    return -1;
}

uint8_t nrf54l15_gpiote20_acquire_task_channel(uint8_t* channel)
{
    if (channel == 0) {
        return 0U;
    }

    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    irq_state_init_once();
    for (uint8_t ch = 0; ch < CORE_GPIOTE20_CHANNEL_COUNT; ++ch) {
        if (g_irq_channels20[ch].in_use == 0U &&
            g_task_channels_in_use[ch] == 0U) {
            g_task_channels_in_use[ch] = 1U;
            *channel = ch;
            __set_PRIMASK(primask);
            return 1U;
        }
    }

    __set_PRIMASK(primask);
    return 0U;
}

void nrf54l15_gpiote20_release_task_channel(uint8_t channel)
{
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    irq_state_init_once();
    if (channel >= CORE_GPIOTE20_CHANNEL_COUNT) {
        __set_PRIMASK(primask);
        return;
    }
    g_task_channels_in_use[channel] = 0U;
    __set_PRIMASK(primask);
}

static uint8_t any_irq_channel_active(uintptr_t base, uint8_t level_only)
{
    irq_channel_t* channels = irq_channels_for_base(base);
    const uint8_t channel_count = gpiote_channel_count(base);
    for (uint8_t ch = 0; ch < channel_count; ++ch) {
        if (channels[ch].in_use != 0U &&
            (level_only == 0U || channels[ch].level_low != 0U)) {
            return 1U;
        }
    }
    return 0U;
}

static void configure_pin_for_interrupt(const pin_desc_t* d, uint8_t level_low)
{
    if (d == 0 || d->valid == 0U) {
        return;
    }

    NRF_GPIO_Type* gpio = gpio_for_port(d->port);
    if (gpio == 0) {
        return;
    }

    const uint32_t bit = (1UL << d->pin);
    uint32_t cnf = gpio->PIN_CNF[d->pin];
    cnf &= ~(GPIO_PIN_CNF_DIR_Msk |
             GPIO_PIN_CNF_INPUT_Msk |
             GPIO_PIN_CNF_SENSE_Msk);
    cnf |= (GPIO_PIN_CNF_DIR_Input << GPIO_PIN_CNF_DIR_Pos);
    cnf |= GPIO_PIN_CNF_INPUT_Connect;
    cnf |= (level_low != 0U)
               ? (GPIO_PIN_CNF_SENSE_Low << GPIO_PIN_CNF_SENSE_Pos)
               : (GPIO_PIN_CNF_SENSE_Disabled << GPIO_PIN_CNF_SENSE_Pos);
    gpio->DIRCLR = bit;
    gpio->PIN_CNF[d->pin] = cnf;
}

static void gpiote_irq_service(uintptr_t base)
{
    if (g_irq_state_initialized == 0U) {
        return;
    }

    irq_channel_t* channels = irq_channels_for_base(base);
    const uint8_t channel_count = gpiote_channel_count(base);
    for (uint8_t ch = 0; ch < channel_count; ++ch) {
        if (channels[ch].in_use == 0U || channels[ch].level_low != 0U) {
            continue;
        }

        volatile uint32_t* event_reg = gpiote_regptr(base, CORE_GPIOTE_EVENTS_IN + ((uintptr_t)ch * sizeof(uint32_t)));
        if (*event_reg == 0U) {
            continue;
        }

        *event_reg = 0U;
        __DSB();

        void (*callback)(void) = channels[ch].callback;
        if (callback != 0) {
            callback();
        }
    }

    volatile uint32_t* port_event =
        gpiote_regptr(base, CORE_GPIOTE_PORT_EVENT_OFFSET);
    if (*port_event == 0U) {
        return;
    }
    *port_event = 0U;
    __DSB();

    for (uint8_t ch = 0; ch < channel_count; ++ch) {
        if (channels[ch].in_use == 0U || channels[ch].level_low == 0U) {
            continue;
        }

        const uint8_t arduino_pin = channels[ch].pin;
        const uint8_t port = channels[ch].port;
        const pin_desc_t d = resolve_pin(arduino_pin);
        NRF_GPIO_Type* gpio = gpio_for_port(port);
        if (gpio == 0 || d.valid == 0U) {
            continue;
        }
        const uint32_t bit = (1UL << d.pin);
        if (((gpio->LATCH | ~gpio->IN) & bit) == 0U) {
            continue;
        }

        const uint8_t pin_in_port = d.pin;
        uint32_t cnf = gpio->PIN_CNF[pin_in_port];
        gpio->PIN_CNF[pin_in_port] = cnf & ~GPIO_PIN_CNF_SENSE_Msk;
        gpio->LATCH = bit;
        void (*callback)(void) = channels[ch].callback;
        if (callback != 0) {
            callback();
        }
        if (channels[ch].in_use != 0U &&
            channels[ch].level_low != 0U &&
            channels[ch].pin == arduino_pin &&
            channels[ch].port == port) {
            cnf = gpio->PIN_CNF[pin_in_port] & ~GPIO_PIN_CNF_SENSE_Msk;
            gpio->PIN_CNF[pin_in_port] =
                cnf | (GPIO_PIN_CNF_SENSE_Low << GPIO_PIN_CNF_SENSE_Pos);
        }
    }
}

void GPIOTE20_0_IRQHandler(void)
{
    gpiote_irq_service((uintptr_t)CORE_GPIOTE20_BASE);
}

void GPIOTE20_1_IRQHandler(void)
{
    gpiote_irq_service((uintptr_t)CORE_GPIOTE20_BASE);
}

void GPIOTE30_0_IRQHandler(void)
{
    gpiote_irq_service((uintptr_t)CORE_GPIOTE30_BASE);
}

void GPIOTE30_1_IRQHandler(void)
{
    gpiote_irq_service((uintptr_t)CORE_GPIOTE30_BASE);
}

void pinMode(uint8_t pin, uint8_t mode)
{
    pin_desc_t d = resolve_pin(pin);
    if (!d.valid) {
        return;
    }

    NRF_GPIO_Type* gpio = gpio_for_port(d.port);
    if (gpio == 0) {
        return;
    }

    const uint32_t bit = (1UL << d.pin);
    uint32_t cnf = gpio->PIN_CNF[d.pin];

    cnf &= ~(GPIO_PIN_CNF_DIR_Msk |
             GPIO_PIN_CNF_INPUT_Msk |
             GPIO_PIN_CNF_PULL_Msk);

    if (mode == OUTPUT) {
        cnf |= (GPIO_PIN_CNF_DIR_Output << GPIO_PIN_CNF_DIR_Pos);
        cnf |= GPIO_PIN_CNF_INPUT_Disconnect;
        cnf |= GPIO_PIN_CNF_PULL_Disabled;
        gpio->DIRSET = bit;
    } else {
        if (analogWriteDisable != 0) {
            analogWriteDisable(pin);
        }

        cnf |= (GPIO_PIN_CNF_DIR_Input << GPIO_PIN_CNF_DIR_Pos);
        cnf |= GPIO_PIN_CNF_INPUT_Connect;

        if (mode == INPUT_PULLUP) {
            cnf |= GPIO_PIN_CNF_PULL_Pullup;
        } else if (mode == INPUT_PULLDOWN) {
            cnf |= GPIO_PIN_CNF_PULL_Pulldown;
        } else {
            cnf |= GPIO_PIN_CNF_PULL_Disabled;
        }

        gpio->DIRCLR = bit;
    }

    gpio->PIN_CNF[d.pin] = cnf;
}

void digitalWrite(uint8_t pin, uint8_t value)
{
    pin_desc_t d = resolve_pin(pin);
    if (!d.valid) {
        return;
    }

    if (analogWriteDisable != 0) {
        analogWriteDisable(pin);
    }

    NRF_GPIO_Type* gpio = gpio_for_port(d.port);
    if (gpio == 0) {
        return;
    }

    const uint32_t bit = (1UL << d.pin);
    if (value != LOW) {
        gpio->OUTSET = bit;
    } else {
        gpio->OUTCLR = bit;
    }
}

int digitalRead(uint8_t pin)
{
    pin_desc_t d = resolve_pin(pin);
    if (!d.valid) {
        return LOW;
    }

    NRF_GPIO_Type* gpio = gpio_for_port(d.port);
    if (gpio == 0) {
        return LOW;
    }

    const uint32_t bit = (1UL << d.pin);
    return ((gpio->IN & bit) != 0U) ? HIGH : LOW;
}

void digitalToggle(uint8_t pin)
{
    if (digitalRead(pin) == HIGH) {
        digitalWrite(pin, LOW);
    } else {
        digitalWrite(pin, HIGH);
    }
}

void attachInterrupt(uint8_t pin, void (*userFunc)(void), int mode)
{
    pin_desc_t d = resolve_pin(pin);
    if (!d.valid || userFunc == 0) {
        return;
    }
    const uintptr_t base = gpiote_base_for_port(d.port);
    if (base == 0U) {
        return;
    }

    if (mode != CHANGE && mode != RISING && mode != FALLING &&
        mode != LOW) {
        return;
    }

    const uint8_t level_low = (mode == LOW) ? 1U : 0U;
    const uint32_t polarity = polarity_from_mode(mode);

    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    irq_state_init_once();

    int8_t channel = find_channel_for_pin(pin, base);
    if (channel < 0) {
        channel = alloc_channel(base);
    }
    if (channel < 0) {
        __set_PRIMASK(primask);
        return;
    }

    const uint8_t ch = (uint8_t)channel;
    irq_channel_t* channels = irq_channels_for_base(base);
    configure_pin_for_interrupt(&d, level_low);

    *gpiote_regptr(base, CORE_GPIOTE_INTENCLR0) =
        (1UL << ch) | CORE_GPIOTE_PORT_INT_MASK;
    *gpiote_regptr(base, CORE_GPIOTE_CONFIG + ((uintptr_t)ch * sizeof(uint32_t))) = 0U;
    *gpiote_regptr(base, CORE_GPIOTE_EVENTS_IN + ((uintptr_t)ch * sizeof(uint32_t))) = 0U;

    channels[ch].in_use = 1U;
    channels[ch].pin = pin;
    channels[ch].port = d.port;
    channels[ch].level_low = level_low;
    channels[ch].callback = userFunc;
    if (pin < IRQ_PIN_MAP_SIZE) {
        g_irq_pin_to_channel[pin] = ch;
    }

    if (level_low != 0U) {
        *gpiote_regptr(base, CORE_GPIOTE_PORT_EVENT_OFFSET) = 0U;
    } else {
        uint32_t config = 0U;
        config |= (CORE_GPIOTE_CONFIG_MODE_EVENT << CORE_GPIOTE_CONFIG_MODE_Pos);
        config |= ((uint32_t)(d.pin & 0x1FU) << CORE_GPIOTE_CONFIG_PSEL_Pos);
        config |= ((uint32_t)(d.port & 0x7U) << CORE_GPIOTE_CONFIG_PORT_Pos);
        config |= (polarity << CORE_GPIOTE_CONFIG_POLARITY_Pos);
        *gpiote_regptr(base, CORE_GPIOTE_CONFIG + ((uintptr_t)ch * sizeof(uint32_t))) = config;
        *gpiote_regptr(base, CORE_GPIOTE_INTENSET0) = (1UL << ch);
    }
    if (any_irq_channel_active(base, 1U) != 0U) {
        *gpiote_regptr(base, CORE_GPIOTE_INTENSET0) = CORE_GPIOTE_PORT_INT_MASK;
    }

    const IRQn_Type irq0 = (base == (uintptr_t)CORE_GPIOTE30_BASE)
                                ? GPIOTE30_0_IRQn : GPIOTE20_0_IRQn;
    NVIC_SetPriority(irq0, 3U);
    NVIC_EnableIRQ(irq0);
    __set_PRIMASK(primask);
}

void detachInterrupt(uint8_t pin)
{
    pin_desc_t d = resolve_pin(pin);
    if (!d.valid || g_irq_state_initialized == 0U) {
        return;
    }
    const uintptr_t base = gpiote_base_for_port(d.port);
    if (base == 0U) {
        return;
    }

    const uint32_t primask = __get_PRIMASK();
    __disable_irq();

    int8_t channel = find_channel_for_pin(pin, base);
    if (channel < 0) {
        __set_PRIMASK(primask);
        return;
    }

    const uint8_t ch = (uint8_t)channel;
    irq_channel_t* channels = irq_channels_for_base(base);

    *gpiote_regptr(base, CORE_GPIOTE_INTENCLR0) = (1UL << ch);
    *gpiote_regptr(base, CORE_GPIOTE_CONFIG + ((uintptr_t)ch * sizeof(uint32_t))) =
        (CORE_GPIOTE_CONFIG_MODE_DISABLED << CORE_GPIOTE_CONFIG_MODE_Pos);
    *gpiote_regptr(base, CORE_GPIOTE_EVENTS_IN + ((uintptr_t)ch * sizeof(uint32_t))) = 0U;

    if (channels[ch].level_low != 0U) {
        NRF_GPIO_Type* gpio = gpio_for_port(d.port);
        if (gpio != 0) {
            gpio->PIN_CNF[d.pin] &= ~GPIO_PIN_CNF_SENSE_Msk;
            gpio->LATCH = (1UL << d.pin);
        }
    }

    channels[ch].in_use = 0U;
    channels[ch].pin = 0xFFU;
    channels[ch].port = 0xFFU;
    channels[ch].level_low = 0U;
    channels[ch].callback = 0;
    if (pin < IRQ_PIN_MAP_SIZE) {
        g_irq_pin_to_channel[pin] = PIN_TO_CHANNEL_UNUSED;
    }

    if (any_irq_channel_active(base, 1U) == 0U) {
        *gpiote_regptr(base, CORE_GPIOTE_INTENCLR0) = CORE_GPIOTE_PORT_INT_MASK;
    }
    if (any_irq_channel_active(base, 0U) == 0U) {
        NVIC_DisableIRQ((base == (uintptr_t)CORE_GPIOTE30_BASE)
                            ? GPIOTE30_0_IRQn : GPIOTE20_0_IRQn);
    }
    __set_PRIMASK(primask);
}

void noInterrupts(void)
{
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    if (g_irq_nest == 0U) {
        g_irq_saved_primask = primask;
    }
    ++g_irq_nest;
}

void interrupts(void)
{
    __disable_irq();
    if (g_irq_nest > 0U) {
        --g_irq_nest;
        if (g_irq_nest == 0U) {
            __set_PRIMASK(g_irq_saved_primask);
        }
    } else {
        __enable_irq();
    }
}

void shiftOut(uint8_t dataPin, uint8_t clockPin, uint8_t bitOrder, uint8_t value)
{
    for (uint8_t i = 0; i < 8; ++i) {
        uint8_t bit_index = (bitOrder == LSBFIRST) ? i : (7 - i);
        digitalWrite(dataPin, (value >> bit_index) & 0x01U);
        digitalWrite(clockPin, HIGH);
        digitalWrite(clockPin, LOW);
    }
}

uint8_t shiftIn(uint8_t dataPin, uint8_t clockPin, uint8_t bitOrder)
{
    uint8_t value = 0;

    for (uint8_t i = 0; i < 8; ++i) {
        digitalWrite(clockPin, HIGH);
        uint8_t bit = (digitalRead(dataPin) == HIGH) ? 1U : 0U;
        uint8_t bit_index = (bitOrder == LSBFIRST) ? i : (7 - i);
        value |= (uint8_t)(bit << bit_index);
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
