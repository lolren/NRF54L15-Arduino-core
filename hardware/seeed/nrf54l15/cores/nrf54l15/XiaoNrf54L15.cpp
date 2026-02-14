#include "XiaoNrf54L15.h"

extern "C" bool arduinoXiaoNrf54l15SetAntenna(uint8_t selection);
extern "C" uint8_t arduinoXiaoNrf54l15GetAntenna(void);

bool XiaoNrf54L15Class::setAntenna(Antenna antenna)
{
    return arduinoXiaoNrf54l15SetAntenna(static_cast<uint8_t>(antenna));
}

XiaoNrf54L15Class::Antenna XiaoNrf54L15Class::getAntenna() const
{
    return (arduinoXiaoNrf54l15GetAntenna() == EXTERNAL) ? EXTERNAL : CERAMIC;
}

bool XiaoNrf54L15Class::useCeramicAntenna()
{
    return setAntenna(CERAMIC);
}

bool XiaoNrf54L15Class::useExternalAntenna()
{
    return setAntenna(EXTERNAL);
}

XiaoNrf54L15Class::RadioProfile XiaoNrf54L15Class::getRadioProfile() const
{
#if defined(ARDUINO_XIAO_NRF54L15_RADIO_BLE_ONLY)
    return RADIO_BLE_ONLY;
#elif defined(ARDUINO_XIAO_NRF54L15_RADIO_DUAL)
    return RADIO_DUAL;
#elif defined(ARDUINO_XIAO_NRF54L15_RADIO_802154_ONLY)
    return RADIO_802154_ONLY;
#else
    return RADIO_UNKNOWN;
#endif
}

int XiaoNrf54L15Class::getBtTxPowerDbm() const
{
#if defined(ARDUINO_XIAO_NRF54L15_BT_TX_PWR_DBM)
    return ARDUINO_XIAO_NRF54L15_BT_TX_PWR_DBM;
#else
    return 0;
#endif
}

bool XiaoNrf54L15Class::isExternalAntennaBuild() const
{
#if defined(ARDUINO_XIAO_NRF54L15_EXT_ANTENNA)
    return true;
#else
    return false;
#endif
}

XiaoNrf54L15Class XiaoNrf54L15;
