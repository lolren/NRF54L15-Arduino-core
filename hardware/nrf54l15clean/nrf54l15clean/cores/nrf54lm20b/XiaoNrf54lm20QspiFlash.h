#ifndef XIAO_NRF54LM20_QSPI_FLASH_H
#define XIAO_NRF54LM20_QSPI_FLASH_H

#include <Arduino.h>

class XiaoNrf54lm20QspiFlash {
public:
    XiaoNrf54lm20QspiFlash();

    bool begin(uint32_t clockHz = 8000000UL);
    void end();

    bool wakeUp();
    bool deepPowerDown();
    bool prepareForSleep();

    bool readJedecId(uint8_t idOut[3]);
    uint32_t jedecId();
    bool readStatus(uint8_t* statusOut);
    bool read(uint32_t address, uint8_t* data, size_t length);
    bool isPresent();

private:
    bool _begun;
    uint32_t _clockHz;

    void configureHoldWriteProtectPins();
    void configureIdlePinsForSleep();
    void select();
    void deselect();
    uint8_t transferByte(uint8_t value);
};

extern XiaoNrf54lm20QspiFlash XiaoQspiFlash;

#endif
