#include "Zigbee.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/sys/util.h>

#if defined(CONFIG_NET_L2_IEEE802154)
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/ieee802154_mgmt.h>
#endif

namespace {
#if defined(CONFIG_NET_L2_IEEE802154)
ZigbeeClass *g_scanOwner = nullptr;
struct net_mgmt_event_callback g_scanCb;
struct ieee802154_req_params g_scanParams = {};
constexpr uint16_t kDefaultInitChannel = 11U;

void scanEventRouter(struct net_mgmt_event_callback *cb, uint64_t mgmtEvent, struct net_if *iface)
{
    ARG_UNUSED(cb);
    ARG_UNUSED(iface);

    if (g_scanOwner != nullptr) {
        g_scanOwner->onScanEvent(mgmtEvent);
    }
}

String formatAddressString(const uint8_t *address, size_t len)
{
    if (address == nullptr || len == 0U) {
        return String("");
    }

    char buffer[3U * IEEE802154_EXT_ADDR_LENGTH] = {0};
    size_t pos = 0;

    for (size_t i = 0; i < len && i < IEEE802154_EXT_ADDR_LENGTH; ++i) {
        int written = snprintf(
            buffer + pos,
            sizeof(buffer) - pos,
            (i + 1U < len) ? "%02X:" : "%02X",
            static_cast<unsigned int>(address[i]));
        if (written <= 0) {
            break;
        }

        pos += static_cast<size_t>(written);
        if (pos >= sizeof(buffer)) {
            break;
        }
    }

    return String(buffer);
}
#endif
} // namespace

bool ZigbeeClass::begin()
{
#if defined(CONFIG_NET_L2_IEEE802154)
    _iface = net_if_get_ieee802154();
    if (_iface == nullptr) {
        _initialized = false;
        _lastError = -ENODEV;
        return false;
    }

    struct net_if *iface = static_cast<struct net_if *>(_iface);
    int err = net_if_up(iface);
    if (err == -ENETDOWN) {
        // IEEE 802.15.4 net_if requires a configured channel before it can be brought up.
        uint16_t defaultChannel = kDefaultInitChannel;
        int setChannelErr = net_mgmt(
            NET_REQUEST_IEEE802154_SET_CHANNEL,
            iface,
            &defaultChannel,
            sizeof(defaultChannel));
        if (setChannelErr != 0) {
            _initialized = false;
            _lastError = setChannelErr;
            return false;
        }

        err = net_if_up(iface);
    }

    if (err != 0 && err != -EALREADY) {
        _initialized = false;
        _lastError = err;
        return false;
    }

    _initialized = true;
    _lastError = 0;
    return true;
#else
    _initialized = false;
    _lastError = -ENOTSUP;
    return false;
#endif
}

bool ZigbeeClass::isAvailable() const
{
    return _initialized;
}

int ZigbeeClass::lastError() const
{
    return _lastError;
}

bool ZigbeeClass::setChannel(uint16_t channelValue)
{
#if defined(CONFIG_NET_L2_IEEE802154)
    if (!_initialized && !begin()) {
        return false;
    }

    int err = net_mgmt(
        NET_REQUEST_IEEE802154_SET_CHANNEL,
        static_cast<struct net_if *>(_iface),
        &channelValue,
        sizeof(channelValue));
    _lastError = err;
    return err == 0;
#else
    ARG_UNUSED(channelValue);
    _lastError = -ENOTSUP;
    return false;
#endif
}

int ZigbeeClass::channel()
{
#if defined(CONFIG_NET_L2_IEEE802154)
    if (!_initialized && !begin()) {
        return -1;
    }

    uint16_t value = 0;
    int err = net_mgmt(
        NET_REQUEST_IEEE802154_GET_CHANNEL,
        static_cast<struct net_if *>(_iface),
        &value,
        sizeof(value));
    _lastError = err;
    return (err == 0) ? static_cast<int>(value) : -1;
#else
    _lastError = -ENOTSUP;
    return -1;
#endif
}

bool ZigbeeClass::setPanId(uint16_t panIdValue)
{
#if defined(CONFIG_NET_L2_IEEE802154)
    if (!_initialized && !begin()) {
        return false;
    }

    int err = net_mgmt(
        NET_REQUEST_IEEE802154_SET_PAN_ID,
        static_cast<struct net_if *>(_iface),
        &panIdValue,
        sizeof(panIdValue));
    _lastError = err;
    return err == 0;
#else
    ARG_UNUSED(panIdValue);
    _lastError = -ENOTSUP;
    return false;
#endif
}

int ZigbeeClass::panId()
{
#if defined(CONFIG_NET_L2_IEEE802154)
    if (!_initialized && !begin()) {
        return -1;
    }

    uint16_t value = 0;
    int err = net_mgmt(
        NET_REQUEST_IEEE802154_GET_PAN_ID,
        static_cast<struct net_if *>(_iface),
        &value,
        sizeof(value));
    _lastError = err;
    return (err == 0) ? static_cast<int>(value) : -1;
#else
    _lastError = -ENOTSUP;
    return -1;
#endif
}

bool ZigbeeClass::setShortAddress(uint16_t shortAddressValue)
{
#if defined(CONFIG_NET_L2_IEEE802154)
    if (!_initialized && !begin()) {
        return false;
    }

    int err = net_mgmt(
        NET_REQUEST_IEEE802154_SET_SHORT_ADDR,
        static_cast<struct net_if *>(_iface),
        &shortAddressValue,
        sizeof(shortAddressValue));
    _lastError = err;
    return err == 0;
#else
    ARG_UNUSED(shortAddressValue);
    _lastError = -ENOTSUP;
    return false;
#endif
}

int ZigbeeClass::shortAddress()
{
#if defined(CONFIG_NET_L2_IEEE802154)
    if (!_initialized && !begin()) {
        return -1;
    }

    uint16_t value = 0;
    int err = net_mgmt(
        NET_REQUEST_IEEE802154_GET_SHORT_ADDR,
        static_cast<struct net_if *>(_iface),
        &value,
        sizeof(value));
    _lastError = err;
    return (err == 0) ? static_cast<int>(value) : -1;
#else
    _lastError = -ENOTSUP;
    return -1;
#endif
}

bool ZigbeeClass::setTxPower(int16_t dbm)
{
#if defined(CONFIG_NET_L2_IEEE802154)
    if (!_initialized && !begin()) {
        return false;
    }

    int err = net_mgmt(
        NET_REQUEST_IEEE802154_SET_TX_POWER,
        static_cast<struct net_if *>(_iface),
        &dbm,
        sizeof(dbm));
    _lastError = err;
    return err == 0;
#else
    ARG_UNUSED(dbm);
    _lastError = -ENOTSUP;
    return false;
#endif
}

int ZigbeeClass::txPower()
{
#if defined(CONFIG_NET_L2_IEEE802154)
    if (!_initialized && !begin()) {
        return 0;
    }

    int16_t value = 0;
    int err = net_mgmt(
        NET_REQUEST_IEEE802154_GET_TX_POWER,
        static_cast<struct net_if *>(_iface),
        &value,
        sizeof(value));
    _lastError = err;
    return (err == 0) ? static_cast<int>(value) : 0;
#else
    _lastError = -ENOTSUP;
    return 0;
#endif
}

bool ZigbeeClass::extendedAddress(uint8_t out[8])
{
#if defined(CONFIG_NET_L2_IEEE802154)
    if (!_initialized && !begin()) {
        return false;
    }

    if (out == nullptr) {
        _lastError = -EINVAL;
        return false;
    }

    int err = net_mgmt(
        NET_REQUEST_IEEE802154_GET_EXT_ADDR,
        static_cast<struct net_if *>(_iface),
        out,
        IEEE802154_EXT_ADDR_LENGTH);
    _lastError = err;
    return err == 0;
#else
    ARG_UNUSED(out);
    _lastError = -ENOTSUP;
    return false;
#endif
}

String ZigbeeClass::extendedAddressString()
{
#if defined(CONFIG_NET_L2_IEEE802154)
    uint8_t address[IEEE802154_EXT_ADDR_LENGTH] = {0};
    if (!extendedAddress(address)) {
        return String("");
    }

    return formatAddressString(address, sizeof(address));
#else
    return String("");
#endif
}

size_t ZigbeeClass::activeScan(uint32_t channelMask, uint32_t durationMs)
{
#if defined(CONFIG_NET_L2_IEEE802154)
    return performScan(NET_REQUEST_IEEE802154_ACTIVE_SCAN, channelMask, durationMs);
#else
    ARG_UNUSED(channelMask);
    ARG_UNUSED(durationMs);
    _lastError = -ENOTSUP;
    return 0;
#endif
}

size_t ZigbeeClass::passiveScan(uint32_t channelMask, uint32_t durationMs)
{
#if defined(CONFIG_NET_L2_IEEE802154)
    return performScan(NET_REQUEST_IEEE802154_PASSIVE_SCAN, channelMask, durationMs);
#else
    ARG_UNUSED(channelMask);
    ARG_UNUSED(durationMs);
    _lastError = -ENOTSUP;
    return 0;
#endif
}

size_t ZigbeeClass::scanResultCount() const
{
    return _scanCount;
}

bool ZigbeeClass::getScanResult(size_t index, ScanResult &result) const
{
    if (index >= _scanCount) {
        return false;
    }

    result = _scanResults[index];
    return true;
}

void ZigbeeClass::clearScanResults()
{
    _scanCount = 0;
    memset(_scanResults, 0, sizeof(_scanResults));
}

uint32_t ZigbeeClass::allChannelMask()
{
#if defined(CONFIG_NET_L2_IEEE802154)
    return IEEE802154_ALL_CHANNELS;
#else
    return 0xFFFFFFFFu;
#endif
}

size_t ZigbeeClass::performScan(uint64_t scanRequest, uint32_t channelMask, uint32_t durationMs)
{
#if defined(CONFIG_NET_L2_IEEE802154)
    if (!_initialized && !begin()) {
        return 0;
    }

    clearScanResults();
    g_scanParams = {};

    g_scanParams.channel_set = (channelMask == 0U) ? IEEE802154_ALL_CHANNELS : channelMask;
    g_scanParams.duration = durationMs;

    net_mgmt_init_event_callback(&g_scanCb, scanEventRouter, NET_EVENT_IEEE802154_SCAN_RESULT);
    net_mgmt_add_event_callback(&g_scanCb);
    g_scanOwner = this;

    int err = -EINVAL;
    if (scanRequest == NET_REQUEST_IEEE802154_ACTIVE_SCAN) {
        err = net_mgmt(
            NET_REQUEST_IEEE802154_ACTIVE_SCAN,
            static_cast<struct net_if *>(_iface),
            &g_scanParams,
            sizeof(g_scanParams));
    } else if (scanRequest == NET_REQUEST_IEEE802154_PASSIVE_SCAN) {
        err = net_mgmt(
            NET_REQUEST_IEEE802154_PASSIVE_SCAN,
            static_cast<struct net_if *>(_iface),
            &g_scanParams,
            sizeof(g_scanParams));
    }

    net_mgmt_del_event_callback(&g_scanCb);
    g_scanOwner = nullptr;

    _lastError = err;
    return (err == 0) ? _scanCount : 0;
#else
    ARG_UNUSED(scanRequest);
    ARG_UNUSED(channelMask);
    ARG_UNUSED(durationMs);
    _lastError = -ENOTSUP;
    return 0;
#endif
}

#if defined(CONFIG_NET_L2_IEEE802154)
void ZigbeeClass::onScanEvent(uint64_t mgmtEvent)
{
    if (mgmtEvent != NET_EVENT_IEEE802154_SCAN_RESULT) {
        return;
    }

    if (_scanCount >= kMaxScanResults) {
        return;
    }

    ScanResult &entry = _scanResults[_scanCount++];
    entry.channel = g_scanParams.channel;
    entry.panId = g_scanParams.pan_id;
    entry.lqi = g_scanParams.lqi;
    entry.associationPermitted = g_scanParams.association_permitted;

    memset(entry.address, 0, sizeof(entry.address));
    entry.addressLength = 0;

    if (g_scanParams.len == IEEE802154_SHORT_ADDR_LENGTH) {
        const uint16_t shortAddr = g_scanParams.short_addr;
        entry.addressLength = IEEE802154_SHORT_ADDR_LENGTH;
        entry.address[0] = static_cast<uint8_t>((shortAddr >> 8) & 0xFFu);
        entry.address[1] = static_cast<uint8_t>(shortAddr & 0xFFu);
    } else if (g_scanParams.len == IEEE802154_EXT_ADDR_LENGTH) {
        entry.addressLength = IEEE802154_EXT_ADDR_LENGTH;
        memcpy(entry.address, g_scanParams.addr, IEEE802154_EXT_ADDR_LENGTH);
    }
}
#endif

ZigbeeClass Zigbee;
