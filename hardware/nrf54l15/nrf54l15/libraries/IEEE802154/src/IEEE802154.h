#ifndef IEEE802154_h
#define IEEE802154_h

#include <Arduino.h>

class IEEE802154Class {
public:
    using ScanResultCallback =
        void (*)(uint16_t channel, uint16_t panId, uint16_t shortAddr, uint8_t lqi,
                 bool associationPermitted);

    bool begin();
    void end();
    bool available() const;

    bool setChannel(uint16_t channel);
    uint16_t channel();

    bool setPanId(uint16_t panId);
    uint16_t panId();

    bool setShortAddress(uint16_t shortAddress);
    uint16_t shortAddress();

    bool setTxPower(int16_t dbm);
    int16_t txPower();

    bool setAckEnabled(bool enabled);
    String extendedAddress() const;

    bool passiveScan(uint32_t channelMask, uint32_t durationMs, ScanResultCallback callback);
    bool activeScan(uint32_t channelMask, uint32_t durationMs, ScanResultCallback callback);

    int lastError() const;

private:
    bool _initialized = false;
    int _lastError = 0;
};

extern IEEE802154Class IEEE802154;

#endif
