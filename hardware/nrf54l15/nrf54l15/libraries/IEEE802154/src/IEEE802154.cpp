#include "IEEE802154.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#if defined(__has_include)
#if __has_include(<generated/zephyr/autoconf.h>)
#include <generated/zephyr/autoconf.h>
#endif
#else
#include <generated/zephyr/autoconf.h>
#endif

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
#if defined(__has_include)
#if __has_include(<zephyr/kernel.h>)
#include <zephyr/kernel.h>
#endif
#if __has_include(<zephyr/net/net_core.h>)
#include <zephyr/net/net_core.h>
#endif
#if __has_include(<zephyr/net/ieee802154.h>)
#include <zephyr/net/ieee802154.h>
#endif
#if __has_include(<zephyr/net/ieee802154_mgmt.h>)
#include <zephyr/net/ieee802154_mgmt.h>
#endif
#if __has_include(<zephyr/net/net_if.h>)
#include <zephyr/net/net_if.h>
#endif
#if __has_include(<zephyr/net/net_mgmt.h>)
#include <zephyr/net/net_mgmt.h>
#endif
#else
#include <zephyr/kernel.h>
#include <zephyr/net/net_core.h>
#include <zephyr/net/ieee802154.h>
#include <zephyr/net/ieee802154_mgmt.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#endif
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace {

IEEE802154Class::ScanResultCallback g_scanCallback = nullptr;

struct net_if* ieee802154Interface()
{
#if defined(CONFIG_NET_L2_IEEE802154)
    return net_if_get_ieee802154();
#else
    return nullptr;
#endif
}

void onScanEvent(struct net_mgmt_event_callback *cb, uint64_t mgmt_event, struct net_if *iface)
{
    ARG_UNUSED(iface);

#if defined(CONFIG_NET_MGMT_EVENT_INFO)
    if (mgmt_event != NET_EVENT_IEEE802154_SCAN_RESULT) {
        return;
    }

    if (cb == nullptr || g_scanCallback == nullptr || cb->info == nullptr ||
        cb->info_length < sizeof(struct ieee802154_req_params)) {
        return;
    }

    const struct ieee802154_req_params *result =
        static_cast<const struct ieee802154_req_params *>(cb->info);
    uint16_t shortAddr = IEEE802154_BROADCAST_ADDRESS;
    if (result->len == IEEE802154_SHORT_ADDR_LENGTH) {
        shortAddr = result->short_addr;
    }

    g_scanCallback(result->channel, result->pan_id, shortAddr, result->lqi,
                   result->association_permitted);
#else
    ARG_UNUSED(cb);
    ARG_UNUSED(mgmt_event);
#endif
}

} // namespace

bool IEEE802154Class::begin()
{
#if !defined(CONFIG_NET_L2_IEEE802154)
    _lastError = -ENOTSUP;
    _initialized = false;
    return false;
#else
    struct net_if *iface = ieee802154Interface();
    if (iface == nullptr) {
        _lastError = -ENODEV;
        _initialized = false;
        return false;
    }

    const int err = net_if_up(iface);
    if (err != 0 && err != -EALREADY) {
        _lastError = err;
        _initialized = false;
        return false;
    }

    _initialized = true;
    _lastError = 0;
    return true;
#endif
}

void IEEE802154Class::end()
{
    struct net_if *iface = ieee802154Interface();
    if (iface != nullptr) {
        (void)net_if_down(iface);
    }
    _initialized = false;
}

bool IEEE802154Class::available() const
{
    struct net_if *iface = ieee802154Interface();
    return (iface != nullptr) && net_if_is_admin_up(iface);
}

bool IEEE802154Class::setChannel(uint16_t channel)
{
#if !defined(CONFIG_NET_L2_IEEE802154)
    _lastError = -ENOTSUP;
    return false;
#else
    if (!_initialized && !begin()) {
        return false;
    }

    struct net_if *iface = ieee802154Interface();
    if (iface == nullptr) {
        _lastError = -ENODEV;
        return false;
    }

    const int err = net_mgmt(NET_REQUEST_IEEE802154_SET_CHANNEL, iface, &channel, sizeof(channel));
    _lastError = err;
    return err == 0;
#endif
}

uint16_t IEEE802154Class::channel()
{
#if !defined(CONFIG_NET_L2_IEEE802154)
    _lastError = -ENOTSUP;
    return 0U;
#else
    if (!_initialized && !begin()) {
        return 0U;
    }

    struct net_if *iface = ieee802154Interface();
    if (iface == nullptr) {
        _lastError = -ENODEV;
        return 0U;
    }

    uint16_t channel = 0U;
    const int err = net_mgmt(NET_REQUEST_IEEE802154_GET_CHANNEL, iface, &channel, sizeof(channel));
    _lastError = err;
    return err == 0 ? channel : 0U;
#endif
}

bool IEEE802154Class::setPanId(uint16_t panId)
{
#if !defined(CONFIG_NET_L2_IEEE802154)
    _lastError = -ENOTSUP;
    return false;
#else
    if (!_initialized && !begin()) {
        return false;
    }

    struct net_if *iface = ieee802154Interface();
    if (iface == nullptr) {
        _lastError = -ENODEV;
        return false;
    }

    const int err = net_mgmt(NET_REQUEST_IEEE802154_SET_PAN_ID, iface, &panId, sizeof(panId));
    _lastError = err;
    return err == 0;
#endif
}

uint16_t IEEE802154Class::panId()
{
#if !defined(CONFIG_NET_L2_IEEE802154)
    _lastError = -ENOTSUP;
    return 0U;
#else
    if (!_initialized && !begin()) {
        return 0U;
    }

    struct net_if *iface = ieee802154Interface();
    if (iface == nullptr) {
        _lastError = -ENODEV;
        return 0U;
    }

    uint16_t panId = 0U;
    const int err = net_mgmt(NET_REQUEST_IEEE802154_GET_PAN_ID, iface, &panId, sizeof(panId));
    _lastError = err;
    return err == 0 ? panId : 0U;
#endif
}

bool IEEE802154Class::setShortAddress(uint16_t shortAddress)
{
#if !defined(CONFIG_NET_L2_IEEE802154)
    _lastError = -ENOTSUP;
    return false;
#else
    if (!_initialized && !begin()) {
        return false;
    }

    struct net_if *iface = ieee802154Interface();
    if (iface == nullptr) {
        _lastError = -ENODEV;
        return false;
    }

    const int err =
        net_mgmt(NET_REQUEST_IEEE802154_SET_SHORT_ADDR, iface, &shortAddress, sizeof(shortAddress));
    _lastError = err;
    return err == 0;
#endif
}

uint16_t IEEE802154Class::shortAddress()
{
#if !defined(CONFIG_NET_L2_IEEE802154)
    _lastError = -ENOTSUP;
    return 0U;
#else
    if (!_initialized && !begin()) {
        return 0U;
    }

    struct net_if *iface = ieee802154Interface();
    if (iface == nullptr) {
        _lastError = -ENODEV;
        return 0U;
    }

    uint16_t shortAddress = 0U;
    const int err =
        net_mgmt(NET_REQUEST_IEEE802154_GET_SHORT_ADDR, iface, &shortAddress, sizeof(shortAddress));
    _lastError = err;
    return err == 0 ? shortAddress : 0U;
#endif
}

bool IEEE802154Class::setTxPower(int16_t dbm)
{
#if !defined(CONFIG_NET_L2_IEEE802154)
    _lastError = -ENOTSUP;
    return false;
#else
    if (!_initialized && !begin()) {
        return false;
    }

    struct net_if *iface = ieee802154Interface();
    if (iface == nullptr) {
        _lastError = -ENODEV;
        return false;
    }

    const int err = net_mgmt(NET_REQUEST_IEEE802154_SET_TX_POWER, iface, &dbm, sizeof(dbm));
    _lastError = err;
    return err == 0;
#endif
}

int16_t IEEE802154Class::txPower()
{
#if !defined(CONFIG_NET_L2_IEEE802154)
    _lastError = -ENOTSUP;
    return 0;
#else
    if (!_initialized && !begin()) {
        return 0;
    }

    struct net_if *iface = ieee802154Interface();
    if (iface == nullptr) {
        _lastError = -ENODEV;
        return 0;
    }

    int16_t dbm = 0;
    const int err = net_mgmt(NET_REQUEST_IEEE802154_GET_TX_POWER, iface, &dbm, sizeof(dbm));
    _lastError = err;
    return err == 0 ? dbm : 0;
#endif
}

bool IEEE802154Class::setAckEnabled(bool enabled)
{
#if !defined(CONFIG_NET_L2_IEEE802154)
    _lastError = -ENOTSUP;
    return false;
#else
    if (!_initialized && !begin()) {
        return false;
    }

    struct net_if *iface = ieee802154Interface();
    if (iface == nullptr) {
        _lastError = -ENODEV;
        return false;
    }

    int err = 0;
    if (enabled) {
        err = net_mgmt(NET_REQUEST_IEEE802154_SET_ACK, iface, nullptr, 0U);
    } else {
        err = net_mgmt(NET_REQUEST_IEEE802154_UNSET_ACK, iface, nullptr, 0U);
    }

    _lastError = err;
    return err == 0;
#endif
}

String IEEE802154Class::extendedAddress() const
{
    struct net_if *iface = ieee802154Interface();
    if (iface == nullptr) {
        return String("");
    }

    const struct net_linkaddr *link = net_if_get_link_addr(iface);
    if (link == nullptr || link->len == 0U) {
        return String("");
    }

    char formatted[3 * IEEE802154_EXT_ADDR_LENGTH] = {0};
    size_t oi = 0;
    for (uint8_t i = 0; i < link->len && i < IEEE802154_EXT_ADDR_LENGTH; i++) {
        const int wrote = snprintf(&formatted[oi], sizeof(formatted) - oi, (i == 0U) ? "%02X" : ":%02X",
                                   link->addr[i]);
        if (wrote <= 0) {
            break;
        }
        oi += static_cast<size_t>(wrote);
        if (oi >= sizeof(formatted)) {
            break;
        }
    }

    return String(formatted);
}

bool IEEE802154Class::passiveScan(uint32_t channelMask, uint32_t durationMs, ScanResultCallback callback)
{
#if !defined(CONFIG_NET_L2_IEEE802154)
    _lastError = -ENOTSUP;
    return false;
#else
    if (channelMask == 0U || callback == nullptr) {
        _lastError = -EINVAL;
        return false;
    }

    if (!_initialized && !begin()) {
        return false;
    }

    struct net_if *iface = ieee802154Interface();
    if (iface == nullptr) {
        _lastError = -ENODEV;
        return false;
    }

    struct ieee802154_req_params req = {};
    req.channel_set = channelMask;
    req.duration = (durationMs == 0U) ? 60U : durationMs;

    g_scanCallback = callback;
#if defined(CONFIG_NET_MGMT_EVENT)
    struct net_mgmt_event_callback eventCb = {};
    net_mgmt_init_event_callback(&eventCb, onScanEvent, NET_EVENT_IEEE802154_SCAN_RESULT);
    net_mgmt_add_event_callback(&eventCb);
#endif

    const int err = net_mgmt(NET_REQUEST_IEEE802154_PASSIVE_SCAN, iface, &req, sizeof(req));

#if defined(CONFIG_NET_MGMT_EVENT)
    net_mgmt_del_event_callback(&eventCb);
#endif
    g_scanCallback = nullptr;

    _lastError = err;
    return err == 0;
#endif
}

bool IEEE802154Class::activeScan(uint32_t channelMask, uint32_t durationMs, ScanResultCallback callback)
{
#if !defined(CONFIG_NET_L2_IEEE802154)
    _lastError = -ENOTSUP;
    return false;
#else
    if (channelMask == 0U || callback == nullptr) {
        _lastError = -EINVAL;
        return false;
    }

    if (!_initialized && !begin()) {
        return false;
    }

    struct net_if *iface = ieee802154Interface();
    if (iface == nullptr) {
        _lastError = -ENODEV;
        return false;
    }

    struct ieee802154_req_params req = {};
    req.channel_set = channelMask;
    req.duration = (durationMs == 0U) ? 60U : durationMs;

    g_scanCallback = callback;
#if defined(CONFIG_NET_MGMT_EVENT)
    struct net_mgmt_event_callback eventCb = {};
    net_mgmt_init_event_callback(&eventCb, onScanEvent, NET_EVENT_IEEE802154_SCAN_RESULT);
    net_mgmt_add_event_callback(&eventCb);
#endif

    const int err = net_mgmt(NET_REQUEST_IEEE802154_ACTIVE_SCAN, iface, &req, sizeof(req));

#if defined(CONFIG_NET_MGMT_EVENT)
    net_mgmt_del_event_callback(&eventCb);
#endif
    g_scanCallback = nullptr;

    _lastError = err;
    return err == 0;
#endif
}

int IEEE802154Class::lastError() const
{
    return _lastError;
}

IEEE802154Class IEEE802154;
