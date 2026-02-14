#include "I2S.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2s.h>
#include <zephyr/kernel.h>

namespace {
constexpr size_t I2S_BLOCK_SIZE = 1024;
constexpr size_t I2S_BLOCK_COUNT = 6;
K_MEM_SLAB_DEFINE_STATIC(g_i2s_tx_slab, I2S_BLOCK_SIZE, I2S_BLOCK_COUNT, 4);
K_MEM_SLAB_DEFINE_STATIC(g_i2s_rx_slab, I2S_BLOCK_SIZE, I2S_BLOCK_COUNT, 4);

const struct device *resolveI2S()
{
#if DT_NODE_HAS_STATUS(DT_ALIAS(arduino_i2s), okay)
    return DEVICE_DT_GET(DT_ALIAS(arduino_i2s));
#elif DT_NODE_HAS_STATUS(DT_NODELABEL(i2s20), okay)
    return DEVICE_DT_GET(DT_NODELABEL(i2s20));
#else
    return nullptr;
#endif
}
} // namespace

bool I2SClass::begin(uint32_t sampleRate, uint8_t bitsPerSample, uint8_t channels)
{
    if (bitsPerSample < 8U || bitsPerSample > 32U || channels == 0U) {
        return false;
    }

    const struct device *dev = resolveI2S();
    if (dev == nullptr || !device_is_ready(dev)) {
        return false;
    }

    end();

    struct i2s_config tx_cfg = {
        .word_size = bitsPerSample,
        .channels = channels,
        .format = (i2s_fmt_t)(I2S_FMT_DATA_FORMAT_I2S | I2S_FMT_CLK_NF_NB),
        .options = (i2s_opt_t)(I2S_OPT_BIT_CLK_MASTER | I2S_OPT_FRAME_CLK_MASTER),
        .frame_clk_freq = sampleRate,
        .mem_slab = &g_i2s_tx_slab,
        .block_size = I2S_BLOCK_SIZE,
        .timeout = 10,
    };

    struct i2s_config rx_cfg = tx_cfg;
    rx_cfg.mem_slab = &g_i2s_rx_slab;

    if (i2s_configure(dev, I2S_DIR_TX, &tx_cfg) != 0) {
        return false;
    }

    if (i2s_configure(dev, I2S_DIR_RX, &rx_cfg) != 0) {
        (void)i2s_trigger(dev, I2S_DIR_TX, I2S_TRIGGER_DROP);
        return false;
    }

    _i2s = dev;
    _configured = true;
    _txStarted = false;
    _rxStarted = false;
    return true;
}

void I2SClass::end()
{
    const struct device *dev = static_cast<const struct device *>(_i2s);
    if (dev != nullptr) {
        (void)i2s_trigger(dev, I2S_DIR_TX, I2S_TRIGGER_DROP);
        (void)i2s_trigger(dev, I2S_DIR_RX, I2S_TRIGGER_DROP);
    }

    _configured = false;
    _txStarted = false;
    _rxStarted = false;
}

int I2SClass::write(const void *buffer, size_t bytes)
{
    if (!_configured || buffer == nullptr || bytes == 0U || bytes > I2S_BLOCK_SIZE) {
        return 0;
    }

    const struct device *dev = static_cast<const struct device *>(_i2s);
    if (dev == nullptr) {
        return 0;
    }

    int err = i2s_buf_write(dev, const_cast<void *>(buffer), bytes);
    if (err != 0) {
        return 0;
    }

    if (!_txStarted) {
        if (i2s_trigger(dev, I2S_DIR_TX, I2S_TRIGGER_START) == 0) {
            _txStarted = true;
        }
    }

    return (int)bytes;
}

int I2SClass::read(void *buffer, size_t bytes)
{
    if (!_configured || buffer == nullptr || bytes == 0U || bytes > I2S_BLOCK_SIZE) {
        return 0;
    }

    const struct device *dev = static_cast<const struct device *>(_i2s);
    if (dev == nullptr) {
        return 0;
    }

    if (!_rxStarted) {
        if (i2s_trigger(dev, I2S_DIR_RX, I2S_TRIGGER_START) == 0) {
            _rxStarted = true;
        }
    }

    size_t got = 0;
    int err = i2s_buf_read(dev, buffer, &got);
    if (err != 0) {
        return 0;
    }

    if (got > bytes) {
        got = bytes;
    }

    return (int)got;
}

bool I2SClass::isReady() const
{
    return _configured;
}

I2SClass I2S;
