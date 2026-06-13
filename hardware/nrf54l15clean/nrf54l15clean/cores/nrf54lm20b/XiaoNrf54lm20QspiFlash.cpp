#include "XiaoNrf54lm20QspiFlash.h"

#include <SPI.h>

namespace {

static constexpr uint8_t kCmdWriteDisable = 0x04U;
static constexpr uint8_t kCmdReadStatus1 = 0x05U;
static constexpr uint8_t kCmdReadData = 0x03U;
static constexpr uint8_t kCmdReadJedecId = 0x9FU;
static constexpr uint8_t kCmdDeepPowerDown = 0xB9U;
static constexpr uint8_t kCmdReleasePowerDown = 0xABU;
static constexpr uint32_t kDefaultTimeoutMs = 100UL;

static bool is_blank_or_floating_id(const uint8_t id[3]) {
    return ((id[0] == 0x00U && id[1] == 0x00U && id[2] == 0x00U) ||
            (id[0] == 0xFFU && id[1] == 0xFFU && id[2] == 0xFFU));
}

}  // namespace

XiaoNrf54lm20QspiFlash XiaoQspiFlash;

extern "C" int xiaoNrf54lm20QspiFlashPrepareForSleep(void) {
    return XiaoQspiFlash.prepareForSleep() ? 1 : 0;
}

XiaoNrf54lm20QspiFlash::XiaoNrf54lm20QspiFlash()
    : _begun(false), _clockHz(8000000UL) {}

bool XiaoNrf54lm20QspiFlash::begin(uint32_t clockHz) {
    if (clockHz == 0U) {
        clockHz = 8000000UL;
    }

    _clockHz = clockHz;
    configureHoldWriteProtectPins();

    pinMode(PIN_QSPI_CS, OUTPUT);
    digitalWrite(PIN_QSPI_CS, HIGH);

    SPI_HS.begin();
    _begun = true;
    return wakeUp();
}

void XiaoNrf54lm20QspiFlash::end() {
    SPI_HS.end();
    _begun = false;
}

bool XiaoNrf54lm20QspiFlash::wakeUp() {
    if (!_begun) {
        return false;
    }

    SPI_HS.beginTransaction(SPISettings(_clockHz, MSBFIRST, SPI_MODE0));
    select();
    (void)transferByte(kCmdReleasePowerDown);
    deselect();
    SPI_HS.endTransaction();
    delayMicroseconds(40);
    return true;
}

bool XiaoNrf54lm20QspiFlash::deepPowerDown() {
    if (!_begun) {
        return false;
    }

    SPI_HS.beginTransaction(SPISettings(_clockHz, MSBFIRST, SPI_MODE0));
    select();
    (void)transferByte(kCmdWriteDisable);
    deselect();
    delayMicroseconds(1);
    select();
    (void)transferByte(kCmdDeepPowerDown);
    deselect();
    SPI_HS.endTransaction();
    delayMicroseconds(10);
    return true;
}

bool XiaoNrf54lm20QspiFlash::prepareForSleep() {
    if (!_begun && !begin()) {
        return false;
    }

    const bool ok = deepPowerDown();
    end();
    configureIdlePinsForSleep();
    return ok;
}

bool XiaoNrf54lm20QspiFlash::readJedecId(uint8_t idOut[3]) {
    if (idOut == nullptr || !_begun) {
        return false;
    }

    if (!wakeUp()) {
        return false;
    }

    SPI_HS.beginTransaction(SPISettings(_clockHz, MSBFIRST, SPI_MODE0));
    select();
    (void)transferByte(kCmdReadJedecId);
    idOut[0] = transferByte(0xFFU);
    idOut[1] = transferByte(0xFFU);
    idOut[2] = transferByte(0xFFU);
    deselect();
    SPI_HS.endTransaction();

    return !is_blank_or_floating_id(idOut);
}

uint32_t XiaoNrf54lm20QspiFlash::jedecId() {
    uint8_t id[3] = {0U, 0U, 0U};
    if (!readJedecId(id)) {
        return 0U;
    }
    return (static_cast<uint32_t>(id[0]) << 16U) |
           (static_cast<uint32_t>(id[1]) << 8U) |
           static_cast<uint32_t>(id[2]);
}

bool XiaoNrf54lm20QspiFlash::readStatus(uint8_t* statusOut) {
    if (!_begun) {
        return false;
    }

    SPI_HS.beginTransaction(SPISettings(_clockHz, MSBFIRST, SPI_MODE0));
    select();
    (void)transferByte(kCmdReadStatus1);
    const uint8_t status = transferByte(0xFFU);
    deselect();
    SPI_HS.endTransaction();

    if (statusOut != nullptr) {
        *statusOut = status;
    }
    return true;
}

bool XiaoNrf54lm20QspiFlash::read(uint32_t address, uint8_t* data, size_t length) {
    if (!_begun || data == nullptr || length == 0U) {
        return false;
    }

    if (!wakeUp()) {
        return false;
    }

    const uint32_t startMs = millis();
    uint8_t status = 0x01U;
    while ((status & 0x01U) != 0U) {
        if (!readStatus(&status)) {
            return false;
        }
        if ((millis() - startMs) > kDefaultTimeoutMs) {
            return false;
        }
        if ((status & 0x01U) != 0U) {
            delay(1);
        }
    }

    SPI_HS.beginTransaction(SPISettings(_clockHz, MSBFIRST, SPI_MODE0));
    select();
    (void)transferByte(kCmdReadData);
    (void)transferByte(static_cast<uint8_t>((address >> 16U) & 0xFFU));
    (void)transferByte(static_cast<uint8_t>((address >> 8U) & 0xFFU));
    (void)transferByte(static_cast<uint8_t>(address & 0xFFU));
    for (size_t i = 0; i < length; ++i) {
        data[i] = transferByte(0xFFU);
    }
    deselect();
    SPI_HS.endTransaction();
    return true;
}

bool XiaoNrf54lm20QspiFlash::isPresent() {
    uint8_t id[3] = {0U, 0U, 0U};
    return readJedecId(id);
}

void XiaoNrf54lm20QspiFlash::configureHoldWriteProtectPins() {
    pinMode(PIN_QSPI_IO2, OUTPUT);
    digitalWrite(PIN_QSPI_IO2, HIGH);
    pinMode(PIN_QSPI_IO3, OUTPUT);
    digitalWrite(PIN_QSPI_IO3, HIGH);
}

void XiaoNrf54lm20QspiFlash::configureIdlePinsForSleep() {
    pinMode(PIN_QSPI_CS, OUTPUT);
    digitalWrite(PIN_QSPI_CS, HIGH);
    pinMode(PIN_QSPI_SCK, OUTPUT);
    digitalWrite(PIN_QSPI_SCK, LOW);
    pinMode(PIN_QSPI_IO0, OUTPUT);
    digitalWrite(PIN_QSPI_IO0, HIGH);
    pinMode(PIN_QSPI_IO1, INPUT_PULLUP);
    configureHoldWriteProtectPins();
}

void XiaoNrf54lm20QspiFlash::select() {
    digitalWrite(PIN_QSPI_CS, LOW);
}

void XiaoNrf54lm20QspiFlash::deselect() {
    digitalWrite(PIN_QSPI_CS, HIGH);
}

uint8_t XiaoNrf54lm20QspiFlash::transferByte(uint8_t value) {
    return SPI_HS.transfer(value);
}
