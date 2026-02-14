#ifndef XIAO_NRF54L15_H
#define XIAO_NRF54L15_H

#include <stdint.h>

class XiaoNrf54L15Class {
public:
    enum Antenna : uint8_t {
        CERAMIC = 0,
        EXTERNAL = 1,
    };

    enum RadioProfile : uint8_t {
        RADIO_BLE_ONLY = 0,
        RADIO_DUAL = 1,
        RADIO_802154_ONLY = 2,
        RADIO_UNKNOWN = 255,
    };

    bool setAntenna(Antenna antenna);
    Antenna getAntenna() const;
    bool useCeramicAntenna();
    bool useExternalAntenna();
    RadioProfile getRadioProfile() const;
    int getBtTxPowerDbm() const;
    bool isExternalAntennaBuild() const;
};

extern XiaoNrf54L15Class XiaoNrf54L15;

#endif
