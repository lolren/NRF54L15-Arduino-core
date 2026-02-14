/*
 * TwoWire (I2C/TWI) Library for nRF54L15
 *
 * Zephyr-backed I2C controller + target implementation.
 *
 * Licensed under the Apache License 2.0
 */

#include "Wire.h"

#include <errno.h>

#include <generated/zephyr/autoconf.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/sys/util.h>

namespace {

const struct device* resolveI2C()
{
#if DT_NODE_HAS_STATUS(DT_ALIAS(xiao_i2c), okay)
    return DEVICE_DT_GET(DT_ALIAS(xiao_i2c));
#elif DT_NODE_HAS_STATUS(DT_NODELABEL(i2c22), okay)
    return DEVICE_DT_GET(DT_NODELABEL(i2c22));
#else
    return nullptr;
#endif
}

uint32_t speedFromClock(uint32_t clockHz)
{
    if (clockHz >= 3400000U) {
        return I2C_SPEED_SET(I2C_SPEED_HIGH);
    }
    if (clockHz >= 1000000U) {
        return I2C_SPEED_SET(I2C_SPEED_FAST_PLUS);
    }
    if (clockHz >= 400000U) {
        return I2C_SPEED_SET(I2C_SPEED_FAST);
    }
    return I2C_SPEED_SET(I2C_SPEED_STANDARD);
}

uint8_t mapControllerError(int err)
{
    switch (err) {
    case 0:
        return 0;
    case -ENXIO:
        return 2; // NACK on address
    case -EIO:
        return 3; // NACK on data
    default:
        return 4; // other error
    }
}

TwoWire* g_wireInstance = nullptr;
struct i2c_target_config g_targetConfig = {};

TwoWire* resolveWireFromTarget(struct i2c_target_config* config)
{
    if (config != &g_targetConfig) {
        return nullptr;
    }
    return g_wireInstance;
}

} // namespace

TwoWire Wire(NRF_TWIM21, PIN_WIRE_SDA, PIN_WIRE_SCL);

TwoWire::TwoWire(NRF_TWIM_Type* twim, uint8_t sda, uint8_t scl)
    : _i2c(resolveI2C())
    , _twim(twim)
    , _sda(sda)
    , _scl(scl)
    , _frequency(400000U)
    , _initialized(false)
    , _txBufferLength(0)
    , _txAddress(0)
    , _rxBufferIndex(0)
    , _rxBufferLength(0)
    , _peek(-1)
    , _targetTxLength(0)
    , _targetTxIndex(0)
    , _targetAddress(0)
    , _targetRegistered(false)
    , _inOnRequestCallback(false)
    , _targetDirection(TARGET_DIR_NONE)
    , _onReceive(nullptr)
    , _onRequest(nullptr)
    , _pendingRepeatedStart(false)
{
    ARG_UNUSED(_twim);
    ARG_UNUSED(_sda);
    ARG_UNUSED(_scl);

    if (g_wireInstance == nullptr) {
        g_wireInstance = this;
    }
}

void TwoWire::begin()
{
    const struct device* dev = static_cast<const struct device*>(_i2c);
    if (dev == nullptr || !device_is_ready(dev)) {
        _initialized = false;
        return;
    }

    (void)i2c_configure(dev, I2C_MODE_CONTROLLER | speedFromClock(_frequency));
    _initialized = true;
}

void TwoWire::begin(uint8_t address)
{
    const struct device* dev = static_cast<const struct device*>(_i2c);
    if (dev == nullptr || !device_is_ready(dev)) {
        _initialized = false;
        return;
    }

    if (!_initialized) {
        begin();
    }

    if (_targetRegistered && _targetAddress == address) {
        return;
    }

    static const struct i2c_target_callbacks targetCallbacks = {
        .write_requested = TwoWire::targetWriteRequestedAdapter,
        .read_requested = TwoWire::targetReadRequestedAdapter,
        .write_received = TwoWire::targetWriteReceivedAdapter,
        .read_processed = TwoWire::targetReadProcessedAdapter,
        .stop = TwoWire::targetStopAdapter,
        .error = nullptr,
    };

    if (_targetRegistered) {
        (void)i2c_target_unregister(dev, &g_targetConfig);
        _targetRegistered = false;
    }

    clearReceiveState();
    clearTargetTxState();

    g_targetConfig = {};
    g_targetConfig.address = address;
    g_targetConfig.flags = 0;
    g_targetConfig.callbacks = &targetCallbacks;

    const int err = i2c_target_register(dev, &g_targetConfig);
    if (err == 0) {
        _targetRegistered = true;
        _targetAddress = address;
        _targetDirection = TARGET_DIR_NONE;
    }
}

void TwoWire::begin(int address)
{
    begin(static_cast<uint8_t>(address));
}

void TwoWire::end()
{
    const struct device* dev = static_cast<const struct device*>(_i2c);
    if (_targetRegistered && dev != nullptr && device_is_ready(dev)) {
        (void)i2c_target_unregister(dev, &g_targetConfig);
    }

    _targetRegistered = false;
    _initialized = false;
    _targetDirection = TARGET_DIR_NONE;
    _inOnRequestCallback = false;
    _pendingRepeatedStart = false;
    clearControllerTxState();
    clearReceiveState();
    clearTargetTxState();
}

void TwoWire::setClock(uint32_t freq)
{
    _frequency = freq;
    if (_initialized) {
        begin();
    }
}

void TwoWire::beginTransmission(uint8_t address)
{
    _txAddress = address;
    clearControllerTxState();
    _pendingRepeatedStart = false;
}

void TwoWire::beginTransmission(int address)
{
    beginTransmission(static_cast<uint8_t>(address));
}

uint8_t TwoWire::endTransmission(bool sendStop)
{
    if (!_initialized) {
        begin();
    }

    const struct device* dev = static_cast<const struct device*>(_i2c);
    if (dev == nullptr || !device_is_ready(dev)) {
        clearControllerTxState();
        _pendingRepeatedStart = false;
        return 4;
    }

    if (!sendStop) {
        _pendingRepeatedStart = true;
        return 0;
    }

    struct i2c_msg msg = {
        .buf = _txBuffer,
        .len = _txBufferLength,
        .flags = I2C_MSG_WRITE | I2C_MSG_STOP,
    };
    int err = i2c_transfer(dev, &msg, 1, _txAddress);

    clearControllerTxState();
    _pendingRepeatedStart = false;
    return mapControllerError(err);
}

uint8_t TwoWire::requestFrom(uint8_t address, uint8_t quantity, uint8_t sendStop)
{
    if (!_initialized) {
        begin();
    }

    const struct device* dev = static_cast<const struct device*>(_i2c);
    if (dev == nullptr || !device_is_ready(dev)) {
        clearReceiveState();
        _pendingRepeatedStart = false;
        clearControllerTxState();
        return 0;
    }

    if (quantity > BUFFER_LENGTH) {
        quantity = BUFFER_LENGTH;
    }
    if (quantity == 0U) {
        clearReceiveState();
        _pendingRepeatedStart = false;
        clearControllerTxState();
        return 0;
    }

    clearReceiveState();

    int err = -EIO;
    if (_pendingRepeatedStart && (_txBufferLength > 0) && (_txAddress == address)) {
        struct i2c_msg msgs[2] = {};
        msgs[0].buf = _txBuffer;
        msgs[0].len = _txBufferLength;
        msgs[0].flags = I2C_MSG_WRITE;

        msgs[1].buf = _rxBuffer;
        msgs[1].len = quantity;
        msgs[1].flags = I2C_MSG_READ | (sendStop ? I2C_MSG_STOP : 0U);

        err = i2c_transfer(dev, msgs, 2, address);
        clearControllerTxState();
    } else {
        struct i2c_msg msg = {};
        msg.buf = _rxBuffer;
        msg.len = quantity;
        msg.flags = I2C_MSG_READ | (sendStop ? I2C_MSG_STOP : 0U);
        err = i2c_transfer(dev, &msg, 1, address);
        clearControllerTxState();
    }

    _pendingRepeatedStart = false;

    if (err != 0) {
        clearReceiveState();
        return 0;
    }

    _rxBufferLength = quantity;
    _rxBufferIndex = 0;
    _peek = -1;
    return _rxBufferLength;
}

uint8_t TwoWire::requestFrom(uint8_t address, size_t quantity, bool sendStop)
{
    if (quantity > BUFFER_LENGTH) {
        quantity = BUFFER_LENGTH;
    }
    return requestFrom(address, static_cast<uint8_t>(quantity), sendStop ? 1U : 0U);
}

uint8_t TwoWire::requestFrom(int address, int quantity)
{
    return requestFrom(address, quantity, 1U);
}

uint8_t TwoWire::requestFrom(int address, int quantity, uint8_t sendStop)
{
    return requestFrom(static_cast<uint8_t>(address), static_cast<uint8_t>(quantity), sendStop);
}

int TwoWire::available(void)
{
    const uint8_t remaining = (_rxBufferIndex < _rxBufferLength) ? (_rxBufferLength - _rxBufferIndex) : 0U;
    return static_cast<int>(remaining + (_peek >= 0 ? 1U : 0U));
}

int TwoWire::read(void)
{
    if (_peek >= 0) {
        const int value = _peek;
        _peek = -1;
        return value;
    }

    if (_rxBufferIndex >= _rxBufferLength) {
        return -1;
    }

    return _rxBuffer[_rxBufferIndex++];
}

int TwoWire::peek(void)
{
    if (_peek >= 0) {
        return _peek;
    }

    if (_rxBufferIndex >= _rxBufferLength) {
        return -1;
    }

    _peek = _rxBuffer[_rxBufferIndex++];
    return _peek;
}

size_t TwoWire::write(uint8_t data)
{
    if (isTargetWriteContext()) {
        if (_targetTxLength >= BUFFER_LENGTH) {
            return 0;
        }
        _targetTxBuffer[_targetTxLength++] = data;
        return 1;
    }

    if (_txBufferLength >= BUFFER_LENGTH) {
        return 0;
    }

    _txBuffer[_txBufferLength++] = data;
    return 1;
}

size_t TwoWire::write(const uint8_t* data, size_t quantity)
{
    size_t written = 0;
    if (data == nullptr) {
        return 0;
    }

    while (written < quantity) {
        if (write(data[written]) == 0) {
            break;
        }
        written++;
    }
    return written;
}

void TwoWire::flush(void)
{
    clearControllerTxState();
    clearTargetTxState();
}

void TwoWire::onReceive(void (*callback)(int))
{
    _onReceive = callback;
}

void TwoWire::onRequest(void (*callback)(void))
{
    _onRequest = callback;
}

bool TwoWire::isTargetWriteContext() const
{
    return _inOnRequestCallback || (_targetRegistered && _targetDirection == TARGET_DIR_READ);
}

void TwoWire::clearControllerTxState()
{
    _txBufferLength = 0;
}

void TwoWire::clearReceiveState()
{
    _rxBufferLength = 0;
    _rxBufferIndex = 0;
    _peek = -1;
}

void TwoWire::clearTargetTxState()
{
    _targetTxLength = 0;
    _targetTxIndex = 0;
}

int TwoWire::provideTargetByte(uint8_t* value)
{
    if (value == nullptr) {
        return -EINVAL;
    }

    if (_targetTxIndex < _targetTxLength) {
        *value = _targetTxBuffer[_targetTxIndex++];
    } else {
        *value = 0xFF;
    }
    return 0;
}

int TwoWire::handleTargetWriteRequested()
{
    _targetDirection = TARGET_DIR_WRITE;
    clearReceiveState();
    return 0;
}

int TwoWire::handleTargetWriteReceived(uint8_t value)
{
    if (_rxBufferLength >= BUFFER_LENGTH) {
        return -ENOMEM;
    }

    _rxBuffer[_rxBufferLength++] = value;
    return 0;
}

int TwoWire::handleTargetReadRequested(uint8_t* value)
{
    _targetDirection = TARGET_DIR_READ;
    clearTargetTxState();

    if (_onRequest != nullptr) {
        _inOnRequestCallback = true;
        _onRequest();
        _inOnRequestCallback = false;
    }

    return provideTargetByte(value);
}

int TwoWire::handleTargetReadProcessed(uint8_t* value)
{
    return provideTargetByte(value);
}

int TwoWire::handleTargetStop()
{
    if (_targetDirection == TARGET_DIR_WRITE && _onReceive != nullptr) {
        _rxBufferIndex = 0;
        _peek = -1;
        _onReceive(static_cast<int>(_rxBufferLength));
    }

    _targetDirection = TARGET_DIR_NONE;
    _inOnRequestCallback = false;
    clearTargetTxState();
    return 0;
}

int TwoWire::targetWriteRequestedAdapter(struct i2c_target_config* config)
{
    TwoWire* wire = resolveWireFromTarget(config);
    return (wire != nullptr) ? wire->handleTargetWriteRequested() : -EINVAL;
}

int TwoWire::targetWriteReceivedAdapter(struct i2c_target_config* config, uint8_t value)
{
    TwoWire* wire = resolveWireFromTarget(config);
    return (wire != nullptr) ? wire->handleTargetWriteReceived(value) : -EINVAL;
}

int TwoWire::targetReadRequestedAdapter(struct i2c_target_config* config, uint8_t* value)
{
    TwoWire* wire = resolveWireFromTarget(config);
    return (wire != nullptr) ? wire->handleTargetReadRequested(value) : -EINVAL;
}

int TwoWire::targetReadProcessedAdapter(struct i2c_target_config* config, uint8_t* value)
{
    TwoWire* wire = resolveWireFromTarget(config);
    return (wire != nullptr) ? wire->handleTargetReadProcessed(value) : -EINVAL;
}

int TwoWire::targetStopAdapter(struct i2c_target_config* config)
{
    TwoWire* wire = resolveWireFromTarget(config);
    return (wire != nullptr) ? wire->handleTargetStop() : -EINVAL;
}
