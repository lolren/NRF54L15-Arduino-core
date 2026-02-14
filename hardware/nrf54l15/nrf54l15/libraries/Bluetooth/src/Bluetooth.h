#ifndef Bluetooth_h
#define Bluetooth_h

#include <Arduino.h>

#include <zephyr/types.h>

class BLEUuid {
public:
    BLEUuid(const char* uuid);
    BLEUuid(uint16_t uuid16);
    
    const char* uuid() const { return _uuid_str; }
    const uint8_t* data() const { return _uuid128; }
    uint16_t uuid16() const;

    char _uuid_str[37];
    uint8_t _uuid128[16];
    bool _is16bit;
};

class BLECharacteristic {
public:
    enum Properties {
        BLERead = 0x01,
        BLEWrite = 0x02,
        BLENotify = 0x04,
        BLEIndicate = 0x08,
        BLEWriteWithoutResponse = 0x10
    };

    BLECharacteristic(const char* uuid, uint8_t properties, size_t valueSize);
    
    bool writeValue(const uint8_t* value, size_t length);
    bool writeValue(const char* value) { return writeValue((const uint8_t*)value, strlen(value)); }
    
    size_t valueLength() const { return _valueLength; }
    const uint8_t* value() const { return _value; }

    void setEventHandler(void (*callback)(BLECharacteristic&));

    BLEUuid _uuid;
    uint8_t _properties;
    uint8_t* _value;
    size_t _valueSize;
    size_t _valueLength;
    void (*_onWrite)(BLECharacteristic&);
    void* _zephyr_attr; 
};

class BLEService {
public:
    BLEService(const char* uuid);
    void addCharacteristic(BLECharacteristic& characteristic);
    
    const char* uuid() const { return _uuid.uuid(); }

    BLEUuid _uuid;
    BLECharacteristic* _characteristics[8];
    uint8_t _numCharacteristics;
    void* _zephyr_svc; 
};

class BluetoothClass {
public:
    static constexpr int ErrorNone = 0;
    static constexpr int ErrorScanFilterNoMatch = -2; // -ENOENT
    using ScanResultCallback = void (*)(const char* address, const char* name, int rssi, uint8_t advType);

    bool begin(const char *deviceName = nullptr);
    void end();

    bool advertise();
    void stopAdvertising();

    bool scan(uint32_t timeoutMs = 5000U);
    bool scanForEach(ScanResultCallback callback, uint32_t timeoutMs = 5000U);
    bool connect(const char* address, uint32_t timeoutMs = 10000U);
    bool connectLastScanResult(uint32_t timeoutMs = 10000U);
    bool disconnect();
    bool connected() const;
    String connectedAddress() const;
    bool setConnectionInterval(uint16_t minUnits, uint16_t maxUnits,
                               uint16_t latency = 0U, uint16_t timeout = 400U);
    bool setScanFilterName(const char* name);
    void clearScanFilterName();
    bool setScanFilterAddress(const char* address);
    void clearScanFilterAddress();
    bool scanFilterEnabled() const;
    String scanFilterName() const;
    bool scanFilterAddressEnabled() const;
    String scanFilterAddress() const;
    int lastError() const;

    bool available() const;
    String address() const;
    String name() const;
    int rssi() const;

    void addService(BLEService& service);
    bool setLocalName(const char* name);
    bool setAdvertisedService(BLEService& service);

    bool _initialized = false;
    bool _advertising = false;
    bool _connected = false;
    bool _hasScanResult = false;
    int _rssi = -127;
    int _lastError = 0;
    char _address[32] = {0};
    char _name[32] = {0};
    char _connectedAddress[32] = {0};
    String _localName;
    BLEService* _advertisedService = nullptr;
    BLEService* _services[8];
    uint8_t _numServices;
};

extern BluetoothClass BLE;

#endif
