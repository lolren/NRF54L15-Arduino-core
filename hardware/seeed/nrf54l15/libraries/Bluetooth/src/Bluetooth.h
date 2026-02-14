#ifndef Bluetooth_h
#define Bluetooth_h

#include <Arduino.h>

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
    bool begin(const char *deviceName = nullptr);
    void end();

    bool advertise();
    void stopAdvertising();

    bool scan(uint32_t timeoutMs = 5000U);
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
    bool _hasScanResult = false;
    int _rssi = -127;
    int _lastError = 0;
    char _address[32] = {0};
    char _name[32] = {0};
    String _localName;
    BLEService* _advertisedService = nullptr;
    BLEService* _services[8];
    uint8_t _numServices;
};

extern BluetoothClass BLE;

#endif