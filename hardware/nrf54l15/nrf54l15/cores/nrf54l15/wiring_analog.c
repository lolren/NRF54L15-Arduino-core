#include "Arduino.h"

#include <generated/zephyr/autoconf.h>

#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/sys/util.h>

extern const struct adc_dt_spec g_adc_map[];
extern const size_t g_adc_map_size;

extern const uint8_t g_pwm_pins[];
extern const struct pwm_dt_spec g_pwm_map[];
extern const size_t g_pwm_map_size;

static uint8_t g_analog_read_resolution = 12;
static uint8_t g_analog_write_resolution = 8;

static const struct pwm_dt_spec *findPwmSpec(uint8_t pin)
{
    for (size_t i = 0; i < g_pwm_map_size; ++i) {
        if (g_pwm_pins[i] == pin) {
            return &g_pwm_map[i];
        }
    }

    return NULL;
}

void analogReference(uint8_t mode)
{
    ARG_UNUSED(mode);
}

void analogReadResolution(uint8_t bits)
{
    g_analog_read_resolution = constrain(bits, 8, 14);
}

void analogWriteResolution(uint8_t bits)
{
    g_analog_write_resolution = constrain(bits, 1, 16);
}

int analogRead(uint8_t pin)
{
    if (pin >= g_adc_map_size) {
        return 0;
    }

    const struct adc_dt_spec *spec = &g_adc_map[pin];
    if (!adc_is_ready_dt(spec)) {
        return 0;
    }

    if (adc_channel_setup_dt(spec) < 0) {
        return 0;
    }

    int16_t sample = 0;
    struct adc_sequence sequence = {
        .buffer = &sample,
        .buffer_size = sizeof(sample),
        .resolution = g_analog_read_resolution,
    };

    if (adc_sequence_init_dt(spec, &sequence) < 0) {
        return 0;
    }

    sequence.resolution = g_analog_read_resolution;

    if (adc_read(spec->dev, &sequence) < 0) {
        return 0;
    }

    if (sample < 0) {
        sample = 0;
    }

    return (int)sample;
}

void analogWrite(uint8_t pin, int value)
{
    const struct pwm_dt_spec *spec = findPwmSpec(pin);
    if (spec == NULL || spec->dev == NULL || !device_is_ready(spec->dev)) {
        return;
    }

    int max_value = (1 << g_analog_write_resolution) - 1;
    int clamped = constrain(value, 0, max_value);

    uint32_t period_us = 2000;
    uint32_t pulse_us = (uint32_t)((uint64_t)clamped * period_us / max_value);

    (void)pwm_set_dt(spec, PWM_USEC(period_us), PWM_USEC(pulse_us));
}

void tone(uint8_t pin, unsigned int frequency, unsigned long duration)
{
    const struct pwm_dt_spec *spec = findPwmSpec(pin);
    if (spec == NULL || spec->dev == NULL || !device_is_ready(spec->dev) || frequency == 0) {
        return;
    }

    uint32_t period_us = 1000000U / frequency;
    uint32_t pulse_us = period_us / 2;
    (void)pwm_set_dt(spec, PWM_USEC(period_us), PWM_USEC(pulse_us));

    if (duration > 0) {
        delay(duration);
        noTone(pin);
    }
}

void noTone(uint8_t pin)
{
    const struct pwm_dt_spec *spec = findPwmSpec(pin);
    if (spec == NULL || spec->dev == NULL || !device_is_ready(spec->dev)) {
        return;
    }

    (void)pwm_set_dt(spec, PWM_USEC(2000), 0U);
}
