#include "SPI.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/spi.h>

namespace {
const struct device *resolveSPI()
{
#if DT_NODE_HAS_STATUS(DT_ALIAS(xiao_spi), okay)
    return DEVICE_DT_GET(DT_ALIAS(xiao_spi));
#else
    return nullptr;
#endif
}

uint16_t operationFromSettings(const SPISettings &settings)
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

SPIClass::SPIClass()
    : _spi(resolveSPI()), _frequency(4000000U), _operation(operationFromSettings(SPISettings())), _csPin(PIN_SPI_SS), _hasCsPin(false), _inTransaction(false)
{
}

void SPIClass::begin(uint8_t csPin)
{
    _csPin = csPin;
    _hasCsPin = true;
    pinMode(_csPin, OUTPUT);
    digitalWrite(_csPin, HIGH);
}

void SPIClass::end()
{
    _inTransaction = false;
}

void SPIClass::beginTransaction(const SPISettings &settings)
{
    _frequency = settings.clock();
    _operation = operationFromSettings(settings);

    _inTransaction = true;
    if (_hasCsPin) {
        digitalWrite(_csPin, LOW);
    }
}

void SPIClass::endTransaction()
{
    if (_hasCsPin) {
        digitalWrite(_csPin, HIGH);
    }
    _inTransaction = false;
}

uint8_t SPIClass::transfer(uint8_t data)
{
    const struct device *dev = static_cast<const struct device *>(_spi);
    if (dev == nullptr || !device_is_ready(dev)) {
        return 0;
    }

    bool autoTransaction = !_inTransaction;
    if (autoTransaction) {
        beginTransaction(SPISettings());
    }

    uint8_t rx = 0;
    struct spi_buf tx_buf = {
        .buf = &data,
        .len = sizeof(data),
    };
    struct spi_buf_set tx = {
        .buffers = &tx_buf,
        .count = 1,
    };

    struct spi_buf rx_buf = {
        .buf = &rx,
        .len = sizeof(rx),
    };
    struct spi_buf_set rx_set = {
        .buffers = &rx_buf,
        .count = 1,
    };

    struct spi_config cfg = {
        .frequency = _frequency,
        .operation = _operation,
        .slave = 0,
        .cs = nullptr,
    };

    (void)spi_transceive(dev, &cfg, &tx, &rx_set);

    if (autoTransaction) {
        endTransaction();
    }

    return rx;
}

void SPIClass::transfer(void *buffer, size_t length)
{
    uint8_t *data = static_cast<uint8_t *>(buffer);
    if (data == nullptr || length == 0) {
        return;
    }

    for (size_t i = 0; i < length; ++i) {
        data[i] = transfer(data[i]);
    }
}

SPIClass SPI;
