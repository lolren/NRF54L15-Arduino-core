#include "Wire.h"

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/sys/util.h>

namespace {
const struct device *resolveI2C()
{
#if DT_NODE_HAS_STATUS(DT_ALIAS(xiao_i2c), okay)
    return DEVICE_DT_GET(DT_ALIAS(xiao_i2c));
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

TwoWire *g_wireInstance = nullptr;
struct i2c_target_config g_targetConfig = {};

TwoWire *resolveWireFromTarget(struct i2c_target_config *config)
{
    if (config != &g_targetConfig) {
        return nullptr;
    }

    return g_wireInstance;
}
} // namespace

TwoWire::TwoWire()
    : _i2c(resolveI2C()),
      _txAddress(0),
      _txLength(0),
      _rxLength(0),
      _rxIndex(0),
      _targetTxLength(0),
      _targetTxIndex(0),
      _peek(-1),
      _clockHz(400000U),
      _targetAddress(0),
      _targetRegistered(false),
      _inOnRequestCallback(false),
      _targetDirection(TARGET_DIR_NONE),
      _onReceive(nullptr),
      _onRequest(nullptr)
{
    if (g_wireInstance == nullptr) {
        g_wireInstance = this;
    }
}

void TwoWire::begin()
{
    const struct device *dev = static_cast<const struct device *>(_i2c);
    if (dev == nullptr || !device_is_ready(dev)) {
        return;
    }

    (void)i2c_configure(dev, I2C_MODE_CONTROLLER | speedFromClock(_clockHz));
}

void TwoWire::begin(uint8_t address)
{
    const struct device *dev = static_cast<const struct device *>(_i2c);
    if (dev == nullptr || !device_is_ready(dev)) {
        return;
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

    int err = i2c_target_register(dev, &g_targetConfig);
    if (err == 0) {
        _targetRegistered = true;
        _targetAddress = address;
        _targetDirection = TARGET_DIR_NONE;
    }
}

void TwoWire::end()
{
    const struct device *dev = static_cast<const struct device *>(_i2c);
    if (_targetRegistered && dev != nullptr && device_is_ready(dev)) {
        (void)i2c_target_unregister(dev, &g_targetConfig);
    }

    _targetRegistered = false;
    _targetDirection = TARGET_DIR_NONE;
    _inOnRequestCallback = false;
    clearControllerTxState();
    clearReceiveState();
    clearTargetTxState();
}

void TwoWire::setClock(uint32_t clockHz)
{
    _clockHz = clockHz;
    begin();
}

void TwoWire::beginTransmission(uint8_t address)
{
    _txAddress = address;
    clearControllerTxState();
}

size_t TwoWire::write(uint8_t data)
{
    if (isTargetWriteContext()) {
        if (_targetTxLength >= sizeof(_targetTxBuffer)) {
            return 0;
        }

        _targetTxBuffer[_targetTxLength++] = data;
        return 1;
    }

    if (_txLength >= sizeof(_txBuffer)) {
        return 0;
    }

    _txBuffer[_txLength++] = data;
    return 1;
}

size_t TwoWire::write(const uint8_t *buffer, size_t size)
{
    size_t written = 0;
    if (buffer == nullptr) {
        return 0;
    }

    if (isTargetWriteContext()) {
        while (written < size && _targetTxLength < sizeof(_targetTxBuffer)) {
            _targetTxBuffer[_targetTxLength++] = buffer[written++];
        }
        return written;
    }

    while (written < size && _txLength < sizeof(_txBuffer)) {
        _txBuffer[_txLength++] = buffer[written++];
    }

    return written;
}

int TwoWire::endTransmission(bool sendStop)
{
    ARG_UNUSED(sendStop);

    const struct device *dev = static_cast<const struct device *>(_i2c);
    if (dev == nullptr || !device_is_ready(dev)) {
        return 4;
    }

    int err = i2c_write(dev, _txBuffer, _txLength, _txAddress);
    clearControllerTxState();

    return (err == 0) ? 0 : 4;
}

uint8_t TwoWire::requestFrom(uint8_t address, uint8_t quantity, bool sendStop)
{
    ARG_UNUSED(sendStop);

    const struct device *dev = static_cast<const struct device *>(_i2c);
    if (dev == nullptr || !device_is_ready(dev)) {
        return 0;
    }

    clearReceiveState();
    _rxLength = quantity > sizeof(_rxBuffer) ? sizeof(_rxBuffer) : quantity;

    int err = i2c_read(dev, _rxBuffer, _rxLength, address);
    if (err != 0) {
        clearReceiveState();
    }

    return static_cast<uint8_t>(_rxLength);
}

int TwoWire::available()
{
    const size_t remaining = (_rxIndex < _rxLength) ? (_rxLength - _rxIndex) : 0U;
    return static_cast<int>(remaining + (_peek >= 0 ? 1U : 0U));
}

void TwoWire::onReceive(ReceiveCallback callback)
{
    _onReceive = callback;
}

void TwoWire::onRequest(RequestCallback callback)
{
    _onRequest = callback;
}

int TwoWire::read()
{
    if (_peek >= 0) {
        int value = _peek;
        _peek = -1;
        return value;
    }

    if (_rxIndex >= _rxLength) {
        return -1;
    }

    return _rxBuffer[_rxIndex++];
}

int TwoWire::peek()
{
    if (_peek >= 0) {
        return _peek;
    }

    if (_rxIndex >= _rxLength) {
        return -1;
    }

    _peek = _rxBuffer[_rxIndex++];
    return _peek;
}

void TwoWire::flush()
{
    clearControllerTxState();
    clearTargetTxState();
}

bool TwoWire::isTargetWriteContext() const
{
    return _inOnRequestCallback || (_targetRegistered && _targetDirection == TARGET_DIR_READ);
}

void TwoWire::clearControllerTxState()
{
    _txLength = 0;
}

void TwoWire::clearReceiveState()
{
    _rxLength = 0;
    _rxIndex = 0;
    _peek = -1;
}

void TwoWire::clearTargetTxState()
{
    _targetTxLength = 0;
    _targetTxIndex = 0;
}

int TwoWire::provideTargetByte(uint8_t *value)
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
    if (_rxLength >= sizeof(_rxBuffer)) {
        return -ENOMEM;
    }

    _rxBuffer[_rxLength++] = value;
    return 0;
}

int TwoWire::handleTargetReadRequested(uint8_t *value)
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

int TwoWire::handleTargetReadProcessed(uint8_t *value)
{
    return provideTargetByte(value);
}

int TwoWire::handleTargetStop()
{
    if (_targetDirection == TARGET_DIR_WRITE && _onReceive != nullptr) {
        _rxIndex = 0;
        _peek = -1;
        _onReceive(static_cast<int>(_rxLength));
    }

    _targetDirection = TARGET_DIR_NONE;
    _inOnRequestCallback = false;
    clearTargetTxState();

    return 0;
}

int TwoWire::targetWriteRequestedAdapter(struct i2c_target_config *config)
{
    TwoWire *wire = resolveWireFromTarget(config);
    return (wire != nullptr) ? wire->handleTargetWriteRequested() : -EINVAL;
}

int TwoWire::targetWriteReceivedAdapter(struct i2c_target_config *config, uint8_t value)
{
    TwoWire *wire = resolveWireFromTarget(config);
    return (wire != nullptr) ? wire->handleTargetWriteReceived(value) : -EINVAL;
}

int TwoWire::targetReadRequestedAdapter(struct i2c_target_config *config, uint8_t *value)
{
    TwoWire *wire = resolveWireFromTarget(config);
    return (wire != nullptr) ? wire->handleTargetReadRequested(value) : -EINVAL;
}

int TwoWire::targetReadProcessedAdapter(struct i2c_target_config *config, uint8_t *value)
{
    TwoWire *wire = resolveWireFromTarget(config);
    return (wire != nullptr) ? wire->handleTargetReadProcessed(value) : -EINVAL;
}

int TwoWire::targetStopAdapter(struct i2c_target_config *config)
{
    TwoWire *wire = resolveWireFromTarget(config);
    return (wire != nullptr) ? wire->handleTargetStop() : -EINVAL;
}

TwoWire Wire;
