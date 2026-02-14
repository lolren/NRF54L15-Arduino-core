#ifndef Zigbee_h
#define Zigbee_h

#include <Arduino.h>

#include <stddef.h>
#include <stdint.h>

class ZigbeeClass {
public:
    struct ScanResult {
        uint16_t channel;
        uint16_t panId;
        uint8_t address[8];
        uint8_t addressLength;
        uint8_t lqi;
        bool associationPermitted;
    };

    bool begin();
    bool isAvailable() const;
    int lastError() const;

    bool setChannel(uint16_t channel);
    int channel();

    bool setPanId(uint16_t panId);
    int panId();

    bool setShortAddress(uint16_t shortAddress);
    int shortAddress();

    bool setTxPower(int16_t dbm);
    int txPower();

    bool extendedAddress(uint8_t out[8]);
    String extendedAddressString();

    size_t activeScan(uint32_t channelMask = 0U, uint32_t durationMs = 200U);
    size_t passiveScan(uint32_t channelMask = 0U, uint32_t durationMs = 200U);
    size_t scanResultCount() const;
    bool getScanResult(size_t index, ScanResult &result) const;
    void clearScanResults();

    static uint32_t allChannelMask();

#if defined(CONFIG_NET_L2_IEEE802154)
    // Internal callback entry used by net_mgmt event adapter.
    void onScanEvent(uint64_t mgmtEvent);
#endif

private:
    static constexpr size_t kMaxScanResults = 16U;

    bool _initialized = false;
    int _lastError = 0;
    ScanResult _scanResults[kMaxScanResults] = {};
    size_t _scanCount = 0;

#if defined(CONFIG_NET_L2_IEEE802154)
    void *_iface = nullptr;
#endif

    size_t performScan(uint64_t scanRequest, uint32_t channelMask, uint32_t durationMs);
};

extern ZigbeeClass Zigbee;

#endif
