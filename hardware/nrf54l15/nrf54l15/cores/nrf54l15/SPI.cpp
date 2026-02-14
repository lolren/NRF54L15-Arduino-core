/*
 * SPI Library for nRF54L15
 *
 * Zephyr-backed hardware SPI implementation.
 *
 * Licensed under the Apache License 2.0
 */

#include "SPI.h"

#include <generated/zephyr/autoconf.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/spi.h>

namespace {

const struct device* resolveSPI()
{
#if DT_NODE_HAS_STATUS(DT_ALIAS(xiao_spi), okay)
    return DEVICE_DT_GET(DT_ALIAS(xiao_spi));
#elif DT_NODE_HAS_STATUS(DT_NODELABEL(spi00), okay)
    return DEVICE_DT_GET(DT_NODELABEL(spi00));
#else
    return nullptr;
#endif
}

uint16_t operationFromSettings(const SPISettings& settings)
{
    uint16_t op = SPI_WORD_SET(8);

    if (settings.bitOrder() == LSBFIRST) {
        op |= SPI_TRANSFER_LSB;
    } else {
        op |= SPI_TRANSFER_MSB;
    }

    switch (settings.dataMode()) {
    case SPI_MODE1:
        op |= SPI_MODE_CPHA;
        break;
    case SPI_MODE2:
        op |= SPI_MODE_CPOL;
        break;
    case SPI_MODE3:
        op |= SPI_MODE_CPOL | SPI_MODE_CPHA;
        break;
    case SPI_MODE0:
    default:
        break;
    }

    return op;
}

} // namespace

// SPI pins are board-mapped to D10/D9/D8 and CS default to D2.
SPIClass SPI(NRF_SPIM20, PIN_SPI_MOSI, PIN_SPI_MISO, PIN_SPI_SCK, PIN_SPI_SS);

SPIClass::SPIClass(NRF_SPIM_Type* spim, uint8_t mosi, uint8_t miso, uint8_t sck, uint8_t cs)
    : _spim(spim)
    , _mosi(mosi)
    , _miso(miso)
    , _sck(sck)
    , _cs(cs)
    , _settings()
    , _initialized(false)
    , _inTransaction(false)
{
}

void SPIClass::begin()
{
    if (_initialized) {
        configurePins();
        return;
    }

    _initialized = true;
    configurePins();
    applySettings();
    _inTransaction = false;
}

void SPIClass::begin(uint8_t csPin)
{
    _cs = csPin;
    begin();
}

void SPIClass::end()
{
    _inTransaction = false;
    _initialized = false;
}

void SPIClass::beginTransaction(SPISettings settings)
{
    if (!_initialized) {
        begin();
    }

    _settings = settings;
    applySettings();
    _inTransaction = true;
    digitalWrite(_cs, LOW);
}

void SPIClass::endTransaction(void)
{
    digitalWrite(_cs, HIGH);
    _inTransaction = false;
}

uint8_t SPIClass::transfer(uint8_t data)
{
    uint8_t rx = 0;
    transfer(&data, &rx, 1);
    return rx;
}

uint16_t SPIClass::transfer16(uint16_t data)
{
    uint8_t tx[2] = {
        static_cast<uint8_t>((data >> 8) & 0xFF),
        static_cast<uint8_t>(data & 0xFF),
    };
    uint8_t rx[2] = {0, 0};

    if (_settings.bitOrder() == LSBFIRST) {
        tx[0] = static_cast<uint8_t>(data & 0xFF);
        tx[1] = static_cast<uint8_t>((data >> 8) & 0xFF);
    }

    transfer(tx, rx, sizeof(tx));

    if (_settings.bitOrder() == LSBFIRST) {
        return static_cast<uint16_t>((static_cast<uint16_t>(rx[1]) << 8) | rx[0]);
    }
    return static_cast<uint16_t>((static_cast<uint16_t>(rx[0]) << 8) | rx[1]);
}

void SPIClass::transfer(void* buf, size_t count)
{
    transfer(buf, buf, count);
}

void SPIClass::transfer(const void* tx_buf, void* rx_buf, size_t count)
{
    if (count == 0) {
        return;
    }

    if (!_initialized) {
        begin();
    }

    const struct device* dev = resolveSPI();
    if (dev == nullptr || !device_is_ready(dev)) {
        return;
    }

    const bool autoTransaction = !_inTransaction;
    if (autoTransaction) {
        beginTransaction(_settings);
    }

    struct spi_buf tx = {
        .buf = const_cast<void*>(tx_buf),
        .len = count,
    };
    struct spi_buf_set tx_set = {
        .buffers = &tx,
        .count = 1,
    };

    struct spi_buf rx = {
        .buf = rx_buf,
        .len = count,
    };
    struct spi_buf_set rx_set = {
        .buffers = &rx,
        .count = 1,
    };

    struct spi_config cfg = {};
    cfg.frequency = getFrequencyValue(_settings.clock());
    cfg.operation = operationFromSettings(_settings);
    cfg.slave = 0;
    cfg.word_delay = 0U;

    const struct spi_buf_set* tx_ptr = (tx_buf != nullptr) ? &tx_set : nullptr;
    const struct spi_buf_set* rx_ptr = (rx_buf != nullptr) ? &rx_set : nullptr;
    (void)spi_transceive(dev, &cfg, tx_ptr, rx_ptr);

    if (autoTransaction) {
        endTransaction();
    }
}

void SPIClass::setBitOrder(uint8_t order)
{
    _settings = SPISettings(_settings.clock(), order, _settings.dataMode());
}

void SPIClass::setDataMode(uint8_t mode)
{
    _settings = SPISettings(_settings.clock(), _settings.bitOrder(), mode);
}

void SPIClass::setClockDivider(uint32_t div)
{
    uint32_t clock = 1000000UL;
    if (div == 0) {
        clock = _settings.clock();
    } else if (div >= 100000UL) {
        clock = div;
    } else {
        clock = static_cast<uint32_t>(F_CPU / div);
    }

    _settings = SPISettings(clock, _settings.bitOrder(), _settings.dataMode());
}

void SPIClass::usingInterrupt(int interruptNumber)
{
    (void)interruptNumber;
}

void SPIClass::notUsingInterrupt(int interruptNumber)
{
    (void)interruptNumber;
}

void SPIClass::attachInterrupt()
{
}

void SPIClass::detachInterrupt()
{
}

void SPIClass::configurePins()
{
    pinMode(_cs, OUTPUT);
    digitalWrite(_cs, HIGH);
}

void SPIClass::applySettings()
{
}

uint32_t SPIClass::getFrequencyValue(uint32_t clockHz)
{
    return clockHz;
}
