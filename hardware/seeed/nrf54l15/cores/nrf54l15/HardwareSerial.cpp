#include "HardwareSerial.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>

namespace {
const struct device *resolveConsoleDevice()
{
#if DT_NODE_HAS_STATUS(DT_CHOSEN(zephyr_console), okay)
    return DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
#elif DT_NODE_HAS_STATUS(DT_CHOSEN(zephyr_shell_uart), okay)
    return DEVICE_DT_GET(DT_CHOSEN(zephyr_shell_uart));
#elif DT_NODE_HAS_STATUS(DT_ALIAS(xiao_serial), okay)
    return DEVICE_DT_GET(DT_ALIAS(xiao_serial));
#else
    return nullptr;
#endif
}

const struct device *resolveSerial1Device()
{
#if DT_NODE_HAS_STATUS(DT_ALIAS(xiao_serial), okay)
    return DEVICE_DT_GET(DT_ALIAS(xiao_serial));
#elif DT_NODE_HAS_STATUS(DT_NODELABEL(uart21), okay)
    return DEVICE_DT_GET(DT_NODELABEL(uart21));
#else
    return nullptr;
#endif
}
} // namespace

HardwareSerial::HardwareSerial(const struct device *uart)
    : _uart(uart != nullptr ? uart : resolveConsoleDevice()), _peek(-1)
{
}

void HardwareSerial::begin(unsigned long baud)
{
    if (_uart == nullptr || !device_is_ready(_uart)) {
        return;
    }

    struct uart_config cfg = {
        .baudrate = static_cast<uint32_t>(baud),
        .parity = UART_CFG_PARITY_NONE,
        .stop_bits = UART_CFG_STOP_BITS_1,
        .data_bits = UART_CFG_DATA_BITS_8,
        .flow_ctrl = UART_CFG_FLOW_CTRL_NONE,
    };

    (void)uart_configure(_uart, &cfg);
}

void HardwareSerial::end()
{
    _peek = -1;
}

int HardwareSerial::available()
{
    if (_peek >= 0) {
        return 1;
    }

    if (_uart == nullptr || !device_is_ready(_uart)) {
        return 0;
    }

    unsigned char c = 0;
    if (uart_poll_in(_uart, &c) == 0) {
        _peek = c;
        return 1;
    }

    return 0;
}

int HardwareSerial::read()
{
    if (_peek >= 0) {
        int value = _peek;
        _peek = -1;
        return value;
    }

    if (_uart == nullptr || !device_is_ready(_uart)) {
        return -1;
    }

    unsigned char c = 0;
    if (uart_poll_in(_uart, &c) == 0) {
        return c;
    }

    return -1;
}

int HardwareSerial::peek()
{
    (void)available();
    return _peek;
}

void HardwareSerial::flush()
{
    k_sleep(K_MSEC(1));
}

size_t HardwareSerial::write(uint8_t value)
{
    if (_uart == nullptr || !device_is_ready(_uart)) {
        return 0;
    }

    uart_poll_out(_uart, value);
    return 1;
}

HardwareSerial::operator bool() const
{
    return (_uart != nullptr) && device_is_ready(_uart);
}

HardwareSerial Serial(resolveConsoleDevice());
HardwareSerial Serial1(resolveSerial1Device());
