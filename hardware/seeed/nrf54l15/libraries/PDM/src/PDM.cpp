#include "PDM.h"

#include <string.h>

#include <zephyr/audio/dmic.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>

namespace {
constexpr size_t PDM_SLAB_BLOCK_SIZE = 2048;
constexpr size_t PDM_SLAB_BLOCK_COUNT = 6;
K_MEM_SLAB_DEFINE_STATIC(g_pdm_slab, PDM_SLAB_BLOCK_SIZE, PDM_SLAB_BLOCK_COUNT, 4);

size_t computeBlockSize(uint32_t sampleRate, uint8_t channels)
{
    size_t bytesPerSample = sizeof(int16_t);
    size_t block = (sampleRate / 50U) * channels * bytesPerSample;
    if (block == 0U) {
        block = 256U;
    }
    if (block > PDM_SLAB_BLOCK_SIZE) {
        block = PDM_SLAB_BLOCK_SIZE;
    }
    return block;
}

const struct device *resolveDMIC()
{
#if DT_NODE_HAS_STATUS(DT_ALIAS(arduino_dmic), okay)
    return DEVICE_DT_GET(DT_ALIAS(arduino_dmic));
#elif DT_NODE_HAS_STATUS(DT_ALIAS(dmic20), okay)
    return DEVICE_DT_GET(DT_ALIAS(dmic20));
#elif DT_NODE_HAS_STATUS(DT_NODELABEL(dmic_dev), okay)
    return DEVICE_DT_GET(DT_NODELABEL(dmic_dev));
#elif DT_NODE_HAS_STATUS(DT_NODELABEL(pdm20), okay)
    return DEVICE_DT_GET(DT_NODELABEL(pdm20));
#else
    return nullptr;
#endif
}
} // namespace

bool PDMClass::begin(uint32_t sampleRate, uint8_t channels)
{
    if (channels == 0U || channels > 2U) {
        return false;
    }

    const struct device *dev = resolveDMIC();
    if (dev == nullptr || !device_is_ready(dev)) {
        return false;
    }

    end();

    static struct pcm_stream_cfg stream;
    static struct dmic_cfg cfg;

    stream.pcm_rate = sampleRate;
    stream.pcm_width = 16;
    stream.block_size = (uint16_t)computeBlockSize(sampleRate, channels);
    stream.mem_slab = &g_pdm_slab;

    memset(&cfg, 0, sizeof(cfg));
    cfg.io.min_pdm_clk_freq = 1000000U;
    cfg.io.max_pdm_clk_freq = 3500000U;
    cfg.io.min_pdm_clk_dc = 40U;
    cfg.io.max_pdm_clk_dc = 60U;
    cfg.streams = &stream;
    cfg.channel.req_num_streams = 1U;
    cfg.channel.req_num_chan = channels;

    cfg.channel.req_chan_map_lo = dmic_build_channel_map(0, 0, PDM_CHAN_LEFT);
    if (channels > 1U) {
        cfg.channel.req_chan_map_lo |= dmic_build_channel_map(1, 0, PDM_CHAN_RIGHT);
    }

    if (dmic_configure(dev, &cfg) < 0) {
        return false;
    }

    if (dmic_trigger(dev, DMIC_TRIGGER_START) < 0) {
        (void)dmic_trigger(dev, DMIC_TRIGGER_RESET);
        return false;
    }

    _dmic = dev;
    _running = true;
    _pendingBlock = nullptr;
    _pendingSize = 0;
    _pendingIndex = 0;
    return true;
}

void PDMClass::end()
{
    const struct device *dev = static_cast<const struct device *>(_dmic);

    if (_pendingBlock != nullptr) {
        k_mem_slab_free(&g_pdm_slab, _pendingBlock);
        _pendingBlock = nullptr;
    }

    if (dev != nullptr && _running) {
        (void)dmic_trigger(dev, DMIC_TRIGGER_STOP);
        (void)dmic_trigger(dev, DMIC_TRIGGER_RESET);
    }

    _pendingSize = 0;
    _pendingIndex = 0;
    _running = false;
}

int PDMClass::available()
{
    if (_pendingBlock != nullptr && _pendingIndex < _pendingSize) {
        return (int)(_pendingSize - _pendingIndex);
    }

    if (!_running) {
        return 0;
    }

    const struct device *dev = static_cast<const struct device *>(_dmic);
    if (dev == nullptr) {
        return 0;
    }

    void *block = nullptr;
    size_t size = 0;
    int ret = dmic_read(dev, 0, &block, &size, 1);
    if (ret != 0 || block == nullptr || size == 0U) {
        return 0;
    }

    _pendingBlock = block;
    _pendingSize = size;
    _pendingIndex = 0;
    return (int)_pendingSize;
}

int PDMClass::read(void *buffer, size_t bytes)
{
    if (buffer == nullptr || bytes == 0U) {
        return 0;
    }

    if (available() <= 0) {
        return 0;
    }

    size_t remaining = _pendingSize - _pendingIndex;
    size_t toCopy = bytes < remaining ? bytes : remaining;

    memcpy(buffer, static_cast<uint8_t *>(_pendingBlock) + _pendingIndex, toCopy);
    _pendingIndex += toCopy;

    if (_pendingIndex >= _pendingSize) {
        k_mem_slab_free(&g_pdm_slab, _pendingBlock);
        _pendingBlock = nullptr;
        _pendingSize = 0;
        _pendingIndex = 0;
    }

    return (int)toCopy;
}

bool PDMClass::isRunning() const
{
    return _running;
}

PDMClass PDM;
