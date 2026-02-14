#include "Bluetooth.h"

#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gap.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

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

void scanCallback(const bt_addr_le_t *addr, int8_t rssi, uint8_t advType, struct net_buf_simple *ad)
{
    ARG_UNUSED(advType);
    char parsedName[32] = {0};
    bt_data_parse(ad, adNameParser, parsedName);

    if (!g_hasScanResult) {
        storeScanResult(addr, rssi, parsedName);
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

ssize_t read_chr(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                 void *buf, uint16_t len, uint16_t offset)
{
    BLECharacteristic *chr = (BLECharacteristic *)attr->user_data;
    return bt_gatt_attr_read(conn, attr, buf, len, offset, chr->value(), chr->valueLength());
}

ssize_t write_chr(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                  const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
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
    if (!_initialized) {
        _numServices = 0;
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
            struct _bt_gatt_ccc *ccc_data = (struct _bt_gatt_ccc*)malloc(sizeof(struct _bt_gatt_ccc));
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

    // Standard Flags
    static const uint8_t ad_flags[] = { BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR };
    
    struct bt_data ad[2];
    uint8_t ad_count = 0;

    ad[ad_count].type = BT_DATA_FLAGS;
    ad[ad_count].data = ad_flags;
    ad[ad_count].data_len = 1;
    ad_count++;

    // Let's use the local name in the AD packet instead of USE_IDENTITY to be safe
    size_t nameLen = strlen(name);
    if (nameLen > 20) nameLen = 20; // Truncate to fit in AD if needed

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

    // Simplified params
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

    g_hasScanResult = false;
    g_lastAddress[0] = '\0';
    g_lastName[0] = '\0';
    g_lastRssi = -127;

    struct bt_le_scan_param active_scan_param = {
        .type = BT_LE_SCAN_TYPE_ACTIVE,
        .options = BT_LE_SCAN_OPT_FILTER_DUPLICATE,
        .interval = BT_GAP_SCAN_FAST_INTERVAL,
        .window = BT_GAP_SCAN_FAST_WINDOW,
    };

    _lastError = runScanWindow(&active_scan_param, timeoutMs);
    _hasScanResult = (g_hasScanResult != false);
    _rssi = g_lastRssi;
    strncpy(_address, g_lastAddress, sizeof(_address) - 1);
    _address[sizeof(_address) - 1] = '\0';
    strncpy(_name, g_lastName, sizeof(_name) - 1);
    _name[sizeof(_name) - 1] = '\0';
    return _hasScanResult;
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