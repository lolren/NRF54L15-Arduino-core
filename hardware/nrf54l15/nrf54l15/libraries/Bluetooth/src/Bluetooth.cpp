#include "Bluetooth.h"

#include <errno.h>
#include <ctype.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#if defined(__has_include)
#if __has_include(<generated/zephyr/autoconf.h>)
#include <generated/zephyr/autoconf.h>
#endif
#if __has_include(<zephyr/bluetooth/bluetooth.h>)
#include <zephyr/bluetooth/bluetooth.h>
#endif
#if __has_include(<zephyr/bluetooth/conn.h>)
#include <zephyr/bluetooth/conn.h>
#endif
#if __has_include(<zephyr/bluetooth/addr.h>)
#include <zephyr/bluetooth/addr.h>
#endif
#if __has_include(<zephyr/bluetooth/gap.h>)
#include <zephyr/bluetooth/gap.h>
#endif
#if __has_include(<zephyr/bluetooth/hci.h>)
#include <zephyr/bluetooth/hci.h>
#endif
#if __has_include(<zephyr/bluetooth/uuid.h>)
#include <zephyr/bluetooth/uuid.h>
#endif
#if __has_include(<zephyr/bluetooth/gatt.h>)
#include <zephyr/bluetooth/gatt.h>
#endif
#if __has_include(<zephyr/kernel.h>)
#include <zephyr/kernel.h>
#endif
#if __has_include(<zephyr/sys/util.h>)
#include <zephyr/sys/util.h>
#endif
#else
#include <generated/zephyr/autoconf.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/addr.h>
#include <zephyr/bluetooth/gap.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#endif

// --- BLEUuid Implementation ---

BLEUuid::BLEUuid(const char* uuid) {
    memset(_uuid128, 0, 16);
    if (!uuid) {
        _uuid_str[0] = '\0';
        _is16bit = true;
        return;
    }
    strncpy(_uuid_str, uuid, sizeof(_uuid_str) - 1);
    _uuid_str[sizeof(_uuid_str) - 1] = '\0';
    
    int i = 0, j = 15;
    while (uuid[i] && j >= 0) {
        if (uuid[i] == '-') { i++; continue; }
        unsigned int val;
        if (sscanf(&uuid[i], "%02x", &val) == 1) {
            _uuid128[j--] = (uint8_t)val;
            i += 2;
        } else {
            break;
        }
    }
    _is16bit = (strlen(uuid) <= 4);
}

BLEUuid::BLEUuid(uint16_t uuid16) {
    snprintf(_uuid_str, sizeof(_uuid_str), "%04x", uuid16);
    memset(_uuid128, 0, sizeof(_uuid128));
    _uuid128[0] = uuid16 & 0xFF;
    _uuid128[1] = (uuid16 >> 8) & 0xFF;
    _is16bit = true;
}

uint16_t BLEUuid::uuid16() const {
    return (_uuid128[1] << 8) | _uuid128[0];
}

// --- BLECharacteristic Implementation ---

BLECharacteristic::BLECharacteristic(const char* uuid, uint8_t properties, size_t valueSize)
    : _uuid(uuid), _properties(properties), _valueSize(valueSize), _valueLength(0), _onWrite(nullptr), _zephyr_attr(nullptr) {
    _value = (uint8_t*)malloc(valueSize);
    memset(_value, 0, valueSize);
}

bool BLECharacteristic::writeValue(const uint8_t* value, size_t length) {
    if (length > _valueSize) length = _valueSize;
    memcpy(_value, value, length);
    _valueLength = length;
    
    if (_zephyr_attr && (_properties & BLENotify)) {
        bt_gatt_notify(NULL, (const struct bt_gatt_attr*)_zephyr_attr, _value, _valueLength);
    }
    return true;
}

void BLECharacteristic::setEventHandler(void (*callback)(BLECharacteristic&)) {
    _onWrite = callback;
}

// --- BLEService Implementation ---

BLEService::BLEService(const char* uuid)
    : _uuid(uuid), _numCharacteristics(0), _zephyr_svc(nullptr) {
}

void BLEService::addCharacteristic(BLECharacteristic& characteristic) {
    if (_numCharacteristics < 8) {
        _characteristics[_numCharacteristics++] = &characteristic;
    }
}

// --- BluetoothClass Implementation ---

namespace {
volatile bool g_hasScanResult = false;
volatile int g_lastRssi = -127;
char g_lastAddress[32];
char g_lastName[32];
bool g_scanNameFilterEnabled = false;
char g_scanNameFilter[32];
bool g_scanAddressFilterEnabled = false;
char g_scanAddressFilter[18];
BluetoothClass::ScanResultCallback g_scanResultCallback = nullptr;
BluetoothClass* g_bleInstance = nullptr;
struct bt_conn* g_activeConnection = nullptr;
bool g_connCallbacksRegistered = false;
volatile bool g_connectInProgress = false;
volatile bool g_connectComplete = false;
volatile int g_connectStatus = 0;

bool registerConnectionCallbacksOnce();
void onConnected(struct bt_conn *conn, uint8_t err);
void onDisconnected(struct bt_conn *conn, uint8_t reason);

bool adNameParser(struct bt_data *data, void *user_data)
{
    if (data->type == BT_DATA_NAME_COMPLETE || data->type == BT_DATA_NAME_SHORTENED) {
        char *name = static_cast<char *>(user_data);
        size_t len = data->data_len < 31 ? data->data_len : 31;
        memcpy(name, data->data, len);
        name[len] = '\0';
        return false;
    }

    return true;
}

void storeScanResult(const bt_addr_le_t *addr, int8_t rssi, const char *name)
{
    bt_addr_le_to_str(addr, g_lastAddress, sizeof(g_lastAddress));
    g_lastRssi = rssi;
    strncpy(g_lastName, name, sizeof(g_lastName) - 1);
    g_lastName[sizeof(g_lastName) - 1] = '\0';
    g_hasScanResult = true;
}

bool scanNameMatchesFilter(const char *name)
{
    if (!g_scanNameFilterEnabled) {
        return true;
    }

    if (name == nullptr || name[0] == '\0') {
        return false;
    }

    return strcmp(name, g_scanNameFilter) == 0;
}

bool normalizeMacAddress(const char *source, char out[18])
{
    if (source == nullptr || out == nullptr) {
        return false;
    }

    size_t oi = 0;
    for (size_t i = 0; source[i] != '\0' && oi < 17; i++) {
        char c = source[i];
        if (c == ' ' || c == '(') {
            break;
        }
        if (c == '-') {
            c = ':';
        }
        out[oi++] = static_cast<char>(toupper(static_cast<unsigned char>(c)));
    }

    if (oi != 17) {
        return false;
    }

    for (size_t i = 0; i < 17; i++) {
        const char c = out[i];
        if ((i % 3U) == 2U) {
            if (c != ':') {
                return false;
            }
            continue;
        }

        if (!isxdigit(static_cast<unsigned char>(c))) {
            return false;
        }
    }

    out[17] = '\0';
    return true;
}

bool parseLeAddress(const char *source, bt_addr_le_t *out)
{
    if (source == nullptr || out == nullptr) {
        return false;
    }

    char address[18] = {0};
    if (!normalizeMacAddress(source, address)) {
        return false;
    }

    const char *type = "public";
    if (strstr(source, "random-id") != nullptr) {
        type = "random-id";
    } else if (strstr(source, "public-id") != nullptr) {
        type = "public-id";
    } else if (strstr(source, "random") != nullptr) {
        type = "random";
    }

    return bt_addr_le_from_str(address, type, out) == 0;
}

void rememberConnectedPeer(BluetoothClass *ble, const struct bt_conn *conn)
{
    if (ble == nullptr || conn == nullptr) {
        return;
    }

    const bt_addr_le_t *peer = bt_conn_get_dst(conn);
    if (peer == nullptr) {
        ble->_connectedAddress[0] = '\0';
        return;
    }

    bt_addr_le_to_str(peer, ble->_connectedAddress, sizeof(ble->_connectedAddress));
}

void onConnected(struct bt_conn *conn, uint8_t err)
{
    if (g_bleInstance == nullptr) {
        return;
    }

    if (err != 0U) {
        g_bleInstance->_connected = false;
        g_bleInstance->_connectedAddress[0] = '\0';
        g_connectStatus = -static_cast<int>(err);
        g_connectComplete = true;
        g_connectInProgress = false;
        return;
    }

    if (g_activeConnection != nullptr && g_activeConnection != conn) {
        bt_conn_unref(g_activeConnection);
        g_activeConnection = nullptr;
    }

    if (g_activeConnection == nullptr) {
        g_activeConnection = bt_conn_ref(conn);
    }

    g_bleInstance->_connected = true;
    rememberConnectedPeer(g_bleInstance, conn);
    g_connectStatus = 0;
    g_connectComplete = true;
    g_connectInProgress = false;
}

void onDisconnected(struct bt_conn *conn, uint8_t reason)
{
    ARG_UNUSED(reason);

    if (g_activeConnection != nullptr && g_activeConnection == conn) {
        bt_conn_unref(g_activeConnection);
        g_activeConnection = nullptr;
    }

    if (g_bleInstance != nullptr) {
        g_bleInstance->_connected = false;
        g_bleInstance->_connectedAddress[0] = '\0';
    }

    if (g_connectInProgress) {
        g_connectStatus = -ENOTCONN;
        g_connectComplete = true;
        g_connectInProgress = false;
    }
}

bool registerConnectionCallbacksOnce()
{
    if (g_connCallbacksRegistered) {
        return true;
    }

    static struct bt_conn_cb cb = {};
    cb.connected = onConnected;
    cb.disconnected = onDisconnected;

    const int err = bt_conn_cb_register(&cb);
    if (err != 0 && err != -EALREADY) {
        return false;
    }

    g_connCallbacksRegistered = true;
    return true;
}

bool scanAddressMatchesFilter(const bt_addr_le_t *addr)
{
    if (!g_scanAddressFilterEnabled) {
        return true;
    }

    if (addr == nullptr) {
        return false;
    }

    char addrString[32] = {0};
    char normalizedAddress[18] = {0};
    bt_addr_le_to_str(addr, addrString, sizeof(addrString));
    if (!normalizeMacAddress(addrString, normalizedAddress)) {
        return false;
    }

    return strcmp(normalizedAddress, g_scanAddressFilter) == 0;
}

void scanCallback(const bt_addr_le_t *addr, int8_t rssi, uint8_t advType, struct net_buf_simple *ad)
{
    char parsedName[32] = {0};
    bt_data_parse(ad, adNameParser, parsedName);

    if (!scanAddressMatchesFilter(addr)) {
        return;
    }

    if (!scanNameMatchesFilter(parsedName)) {
        return;
    }

    if (g_scanResultCallback != nullptr) {
        char callbackAddress[32] = {0};
        bt_addr_le_to_str(addr, callbackAddress, sizeof(callbackAddress));
        g_scanResultCallback(callbackAddress, parsedName, rssi, advType);
    }

    if (!g_hasScanResult) {
        storeScanResult(addr, rssi, parsedName);
        return;
    }

    if (g_scanNameFilterEnabled) {
        if (rssi > g_lastRssi) {
            storeScanResult(addr, rssi, parsedName);
        }
        return;
    }

    const bool currentHasName = g_lastName[0] != '\0';
    const bool candidateHasName = parsedName[0] != '\0';
    const bool preferByName = candidateHasName && !currentHasName;
    const bool preferByRssi = (candidateHasName == currentHasName) && (rssi > g_lastRssi);
    if (preferByName || preferByRssi) {
        storeScanResult(addr, rssi, parsedName);
    }
}

int runScanWindow(const struct bt_le_scan_param *scanParam, uint32_t timeoutMs)
{
    if (timeoutMs < 100U) {
        timeoutMs = 100U;
    }

    int err = bt_le_scan_start(scanParam, scanCallback);
    if (err == -EALREADY) {
        (void)bt_le_scan_stop();
        err = bt_le_scan_start(scanParam, scanCallback);
    }
    if (err != 0) {
        return err;
    }

    int64_t endAt = k_uptime_get() + timeoutMs;
    while (k_uptime_get() < endAt) {
        k_sleep(K_MSEC(25));
    }

    (void)bt_le_scan_stop();
    return 0;
}

bool runScan(BluetoothClass &ble, uint32_t timeoutMs, BluetoothClass::ScanResultCallback callback)
{
    g_hasScanResult = false;
    g_lastAddress[0] = '\0';
    g_lastName[0] = '\0';
    g_lastRssi = -127;
    g_scanResultCallback = callback;

    struct bt_le_scan_param active_scan_param = {
        .type = BT_LE_SCAN_TYPE_ACTIVE,
        .options = BT_LE_SCAN_OPT_FILTER_DUPLICATE,
        .interval = BT_GAP_SCAN_FAST_INTERVAL,
        .window = BT_GAP_SCAN_FAST_WINDOW,
        .timeout = 0U,
        .interval_coded = 0U,
        .window_coded = 0U,
    };

    ble._lastError = runScanWindow(&active_scan_param, timeoutMs);
    ble._hasScanResult = (g_hasScanResult != false);
    ble._rssi = g_lastRssi;
    strncpy(ble._address, g_lastAddress, sizeof(ble._address) - 1);
    ble._address[sizeof(ble._address) - 1] = '\0';
    strncpy(ble._name, g_lastName, sizeof(ble._name) - 1);
    ble._name[sizeof(ble._name) - 1] = '\0';

    if (ble._lastError == 0 && !ble._hasScanResult && (g_scanNameFilterEnabled || g_scanAddressFilterEnabled)) {
        ble._lastError = BluetoothClass::ErrorScanFilterNoMatch;
    }

    g_scanResultCallback = nullptr;
    return ble._hasScanResult;
}

ssize_t read_chr(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                 void *buf, uint16_t len, uint16_t offset)
{
    BLECharacteristic *chr = (BLECharacteristic *)attr->user_data;
    return bt_gatt_attr_read(conn, attr, buf, len, offset, chr->value(), chr->valueLength());
}

ssize_t write_chr(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                  const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
    ARG_UNUSED(conn);
    ARG_UNUSED(flags);

    BLECharacteristic *chr = (BLECharacteristic *)attr->user_data;
    if (offset + len > chr->_valueSize) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }
    memcpy(chr->_value + offset, buf, len);
    chr->_valueLength = offset + len;
    
    if (chr->_onWrite) {
        chr->_onWrite(*chr);
    }
    return len;
}

} // namespace

bool BluetoothClass::begin(const char *deviceName)
{
    g_bleInstance = this;

    if (!_initialized) {
        _numServices = 0;
        _connected = false;
        _connectedAddress[0] = '\0';
    }

    if (_initialized) {
        _lastError = 0;
        return true;
    }

    int err = bt_enable(NULL);
    if (err != 0 && err != -EALREADY) {
        _lastError = err;
        return false;
    }

    if (!registerConnectionCallbacksOnce()) {
        _lastError = -EIO;
        return false;
    }

    if (deviceName != nullptr && strlen(deviceName) > 0) {
        (void)bt_set_name(deviceName);
        _localName = deviceName;
    } else {
        _localName = bt_get_name();
    }

    _lastError = 0;
    _initialized = true;
    return true;
}

void BluetoothClass::end() {
    if (_initialized) {
        stopAdvertising();
        (void)disconnect();
        _initialized = false;
    }
}

bool BluetoothClass::setLocalName(const char* name) {
    _localName = name;
    return bt_set_name(name) == 0;
}

void BluetoothClass::addService(BLEService& service) {
    size_t attr_count = 1; 
    for (uint8_t i = 0; i < service._numCharacteristics; i++) {
        BLECharacteristic* chr = service._characteristics[i];
        attr_count += 2;
        if (chr->_properties & BLECharacteristic::BLENotify) {
            attr_count += 1;
        }
    }

    struct bt_gatt_attr *attrs = (struct bt_gatt_attr*)malloc(sizeof(struct bt_gatt_attr) * attr_count);
    size_t ai = 0;

    struct bt_uuid *svc_uuid;
    if (service._uuid._is16bit) {
        struct bt_uuid_16 *u16 = (struct bt_uuid_16*)malloc(sizeof(struct bt_uuid_16));
        u16->uuid.type = BT_UUID_TYPE_16;
        u16->val = service._uuid.uuid16();
        svc_uuid = (struct bt_uuid *)u16;
    } else {
        struct bt_uuid_128 *u128 = (struct bt_uuid_128*)malloc(sizeof(struct bt_uuid_128));
        u128->uuid.type = BT_UUID_TYPE_128;
        memcpy(u128->val, service._uuid.data(), 16);
        svc_uuid = (struct bt_uuid *)u128;
    }

    attrs[ai].uuid = BT_UUID_GATT_PRIMARY;
    attrs[ai].perm = BT_GATT_PERM_READ;
    attrs[ai].read = bt_gatt_attr_read_service;
    attrs[ai].write = NULL;
    attrs[ai].user_data = svc_uuid;
    ai++;

    for (uint8_t i = 0; i < service._numCharacteristics; i++) {
        BLECharacteristic* chr = service._characteristics[i];
        struct bt_uuid *chr_uuid;
        if (chr->_uuid._is16bit) {
            struct bt_uuid_16 *u16 = (struct bt_uuid_16*)malloc(sizeof(struct bt_uuid_16));
            u16->uuid.type = BT_UUID_TYPE_16;
            u16->val = chr->_uuid.uuid16();
            chr_uuid = (struct bt_uuid *)u16;
        } else {
            struct bt_uuid_128 *u128 = (struct bt_uuid_128*)malloc(sizeof(struct bt_uuid_128));
            u128->uuid.type = BT_UUID_TYPE_128;
            memcpy(u128->val, chr->_uuid.data(), 16);
            chr_uuid = (struct bt_uuid *)u128;
        }

        uint8_t props = 0;
        uint8_t perm = 0;
        if (chr->_properties & BLECharacteristic::BLERead) {
            props |= BT_GATT_CHRC_READ;
            perm |= BT_GATT_PERM_READ;
        }
        if (chr->_properties & BLECharacteristic::BLEWrite) {
            props |= BT_GATT_CHRC_WRITE;
            perm |= BT_GATT_PERM_WRITE;
        }
        if (chr->_properties & BLECharacteristic::BLEWriteWithoutResponse) {
            props |= BT_GATT_CHRC_WRITE_WITHOUT_RESP;
            perm |= BT_GATT_PERM_WRITE;
        }
        if (chr->_properties & BLECharacteristic::BLENotify) {
            props |= BT_GATT_CHRC_NOTIFY;
        }

        struct bt_gatt_chrc *chrc_data = (struct bt_gatt_chrc*)malloc(sizeof(struct bt_gatt_chrc));
        chrc_data->uuid = chr_uuid;
        chrc_data->value_handle = 0;
        chrc_data->properties = props;

        attrs[ai].uuid = BT_UUID_GATT_CHRC;
        attrs[ai].perm = BT_GATT_PERM_READ;
        attrs[ai].read = bt_gatt_attr_read_chrc;
        attrs[ai].write = NULL;
        attrs[ai].user_data = chrc_data;
        ai++;

        attrs[ai].uuid = chr_uuid;
        attrs[ai].perm = perm;
        attrs[ai].read = (props & BT_GATT_CHRC_READ) ? read_chr : NULL;
        attrs[ai].write = (props & (BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP)) ? write_chr : NULL;
        attrs[ai].user_data = chr;
        chr->_zephyr_attr = &attrs[ai];
        ai++;

        if (chr->_properties & BLECharacteristic::BLENotify) {
            struct bt_gatt_ccc_managed_user_data *ccc_data =
                (struct bt_gatt_ccc_managed_user_data*)malloc(sizeof(struct bt_gatt_ccc_managed_user_data));
            memset(ccc_data, 0, sizeof(*ccc_data));
            attrs[ai].uuid = BT_UUID_GATT_CCC;
            attrs[ai].perm = BT_GATT_PERM_READ | BT_GATT_PERM_WRITE;
            attrs[ai].read = bt_gatt_attr_read_ccc;
            attrs[ai].write = bt_gatt_attr_write_ccc;
            attrs[ai].user_data = ccc_data;
            ai++;
        }
    }

    struct bt_gatt_service *zephyr_svc = (struct bt_gatt_service*)malloc(sizeof(struct bt_gatt_service));
    zephyr_svc->attrs = attrs;
    zephyr_svc->attr_count = ai;
    
    bt_gatt_service_register(zephyr_svc);
    service._zephyr_svc = zephyr_svc;
    if (_numServices < 8) {
        _services[_numServices++] = &service;
    }
}

bool BluetoothClass::setAdvertisedService(BLEService& service) {
    _advertisedService = &service;
    return true;
}

bool BluetoothClass::advertise()
{
    if (!_initialized && !begin(nullptr)) {
        return false;
    }

    const char *name = _localName.c_str();
    if (name == nullptr || name[0] == '\0') {
        name = "XIAO-nRF54L15";
    }

    static const uint8_t ad_flags[] = { BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR };
    
    struct bt_data ad[2];
    uint8_t ad_count = 0;

    ad[ad_count].type = BT_DATA_FLAGS;
    ad[ad_count].data = ad_flags;
    ad[ad_count].data_len = 1;
    ad_count++;

    size_t nameLen = strlen(name);
    if (nameLen > 20) nameLen = 20;

    ad[ad_count].type = BT_DATA_NAME_COMPLETE;
    ad[ad_count].data = (const uint8_t*)name;
    ad[ad_count].data_len = (uint8_t)nameLen;
    ad_count++;

    struct bt_data sd[1];
    uint8_t sd_count = 0;

    if (_advertisedService) {
        if (_advertisedService->_uuid._is16bit) {
            sd[sd_count].type = BT_DATA_UUID16_ALL;
            static uint16_t u16;
            u16 = _advertisedService->_uuid.uuid16();
            sd[sd_count].data = (const uint8_t*)&u16;
            sd[sd_count].data_len = 2;
            sd_count++;
        } else {
            sd[sd_count].type = BT_DATA_UUID128_ALL;
            sd[sd_count].data = _advertisedService->_uuid.data();
            sd[sd_count].data_len = 16;
            sd_count++;
        }
    }

    if (_advertising) {
        (void)bt_le_adv_stop();
        _advertising = false;
    }

    const struct bt_le_adv_param adv_param = BT_LE_ADV_PARAM_INIT(
        BT_LE_ADV_OPT_CONN,
        BT_GAP_ADV_FAST_INT_MIN_1,
        BT_GAP_ADV_FAST_INT_MAX_1,
        NULL);

    int err = bt_le_adv_start(&adv_param, ad, ad_count, sd_count > 0 ? sd : NULL, sd_count);
    if (err != 0) {
        _lastError = err;
        return false;
    }

    _lastError = 0;
    _advertising = true;
    return true;
}

void BluetoothClass::stopAdvertising()
{
    if (_advertising) {
        (void)bt_le_adv_stop();
    }
    _advertising = false;
}

bool BluetoothClass::scan(uint32_t timeoutMs)
{
    if (!_initialized && !begin(nullptr)) {
        _lastError = -EAGAIN;
        return false;
    }

    return runScan(*this, timeoutMs, nullptr);
}

bool BluetoothClass::scanForEach(ScanResultCallback callback, uint32_t timeoutMs)
{
    if (callback == nullptr) {
        _lastError = -EINVAL;
        return false;
    }

    if (!_initialized && !begin(nullptr)) {
        _lastError = -EAGAIN;
        return false;
    }

    return runScan(*this, timeoutMs, callback);
}

bool BluetoothClass::connect(const char* address, uint32_t timeoutMs)
{
    if (address == nullptr || address[0] == '\0') {
        _lastError = -EINVAL;
        return false;
    }

    if (!_initialized && !begin(nullptr)) {
        _lastError = -EAGAIN;
        return false;
    }

    bt_addr_le_t peer = {};
    if (!parseLeAddress(address, &peer)) {
        _lastError = -EINVAL;
        return false;
    }

    if (g_activeConnection != nullptr) {
        if (!disconnect()) {
            return false;
        }
    }

    struct bt_conn *pending = nullptr;
    g_connectInProgress = true;
    g_connectComplete = false;
    g_connectStatus = -EINPROGRESS;

    const struct bt_conn_le_create_param createParam = BT_CONN_LE_CREATE_PARAM_INIT(
        BT_CONN_LE_OPT_NONE, BT_GAP_SCAN_FAST_INTERVAL, BT_GAP_SCAN_FAST_INTERVAL);
    const struct bt_le_conn_param connParam = BT_LE_CONN_PARAM_INIT(
        BT_GAP_INIT_CONN_INT_MIN, BT_GAP_INIT_CONN_INT_MAX, 0, BT_GAP_MS_TO_CONN_TIMEOUT(4000));
    int err = bt_conn_le_create(&peer, &createParam, &connParam, &pending);
    if (err != 0) {
        g_connectInProgress = false;
        g_connectComplete = true;
        g_connectStatus = err;
        _lastError = err;
        return false;
    }

    if (timeoutMs < 100U) {
        timeoutMs = 100U;
    }

    const int64_t endAt = k_uptime_get() + timeoutMs;
    while (!g_connectComplete && k_uptime_get() < endAt) {
        k_sleep(K_MSEC(20));
    }

    if (!g_connectComplete) {
        (void)bt_conn_disconnect(pending, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
        g_connectInProgress = false;
        g_connectStatus = -ETIMEDOUT;
    }

    bt_conn_unref(pending);

    _connected = (g_connectStatus == 0) && (g_activeConnection != nullptr);
    if (!_connected) {
        _connectedAddress[0] = '\0';
    }

    _lastError = g_connectStatus;
    return _connected;
}

bool BluetoothClass::connectLastScanResult(uint32_t timeoutMs)
{
    if (!_hasScanResult || _address[0] == '\0') {
        _lastError = -ENOENT;
        return false;
    }

    return connect(_address, timeoutMs);
}

bool BluetoothClass::disconnect()
{
    if (g_activeConnection == nullptr) {
        _connected = false;
        _connectedAddress[0] = '\0';
        _lastError = 0;
        return true;
    }

    int err = bt_conn_disconnect(g_activeConnection, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
    if (err == -ENOTCONN) {
        bt_conn_unref(g_activeConnection);
        g_activeConnection = nullptr;
        _connected = false;
        _connectedAddress[0] = '\0';
        _lastError = 0;
        return true;
    }

    if (err != 0) {
        _lastError = err;
        return false;
    }

    const int64_t endAt = k_uptime_get() + 3000;
    while (g_activeConnection != nullptr && k_uptime_get() < endAt) {
        k_sleep(K_MSEC(20));
    }

    _connected = (g_activeConnection != nullptr);
    if (!_connected) {
        _connectedAddress[0] = '\0';
        _lastError = 0;
        return true;
    }

    _lastError = -ETIMEDOUT;
    return false;
}

bool BluetoothClass::connected() const
{
    return _connected && (g_activeConnection != nullptr);
}

String BluetoothClass::connectedAddress() const
{
    return String(_connectedAddress);
}

bool BluetoothClass::setConnectionInterval(uint16_t minUnits, uint16_t maxUnits,
                                           uint16_t latency, uint16_t timeout)
{
    if (g_activeConnection == nullptr || !_connected) {
        _lastError = -ENOTCONN;
        return false;
    }

    if (minUnits < 6U) {
        minUnits = 6U;
    }
    if (maxUnits < minUnits) {
        maxUnits = minUnits;
    }

    struct bt_le_conn_param param = {
        .interval_min = minUnits,
        .interval_max = maxUnits,
        .latency = latency,
        .timeout = timeout,
    };

    const int err = bt_conn_le_param_update(g_activeConnection, &param);
    _lastError = err;
    return err == 0;
}

bool BluetoothClass::setScanFilterName(const char* name)
{
    if (name == nullptr) {
        _lastError = -EINVAL;
        return false;
    }

    const size_t len = strlen(name);
    if (len == 0 || len >= sizeof(g_scanNameFilter)) {
        _lastError = -EINVAL;
        return false;
    }

    strncpy(g_scanNameFilter, name, sizeof(g_scanNameFilter) - 1);
    g_scanNameFilter[sizeof(g_scanNameFilter) - 1] = '\0';
    g_scanNameFilterEnabled = true;
    _lastError = 0;
    return true;
}

void BluetoothClass::clearScanFilterName()
{
    g_scanNameFilter[0] = '\0';
    g_scanNameFilterEnabled = false;
}

bool BluetoothClass::setScanFilterAddress(const char* address)
{
    char normalizedAddress[18] = {0};
    if (!normalizeMacAddress(address, normalizedAddress)) {
        _lastError = -EINVAL;
        return false;
    }

    strncpy(g_scanAddressFilter, normalizedAddress, sizeof(g_scanAddressFilter) - 1);
    g_scanAddressFilter[sizeof(g_scanAddressFilter) - 1] = '\0';
    g_scanAddressFilterEnabled = true;
    _lastError = 0;
    return true;
}

void BluetoothClass::clearScanFilterAddress()
{
    g_scanAddressFilter[0] = '\0';
    g_scanAddressFilterEnabled = false;
}

bool BluetoothClass::scanFilterEnabled() const
{
    return g_scanNameFilterEnabled || g_scanAddressFilterEnabled;
}

String BluetoothClass::scanFilterName() const
{
    if (!g_scanNameFilterEnabled) {
        return String("");
    }
    return String(g_scanNameFilter);
}

bool BluetoothClass::scanFilterAddressEnabled() const
{
    return g_scanAddressFilterEnabled;
}

String BluetoothClass::scanFilterAddress() const
{
    if (!g_scanAddressFilterEnabled) {
        return String("");
    }
    return String(g_scanAddressFilter);
}

bool BluetoothClass::available() const
{
    return _hasScanResult;
}

String BluetoothClass::address() const
{
    return String(_address);
}

String BluetoothClass::name() const
{
    return String(_name);
}

int BluetoothClass::rssi() const
{
    return _rssi;
}

int BluetoothClass::lastError() const
{
    return _lastError;
}

BluetoothClass BLE;
