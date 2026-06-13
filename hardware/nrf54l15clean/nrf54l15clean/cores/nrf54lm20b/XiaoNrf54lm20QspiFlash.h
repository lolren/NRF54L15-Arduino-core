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

    bool runCommand(uint8_t command);
    bool readCommand(uint8_t command, uint8_t* data, size_t length);
    bool writeEnable();
    bool waitReady(uint32_t timeoutMs = 100UL);
    bool eraseSector(uint32_t address);
    bool eraseChip(uint32_t timeoutMs = 60000UL);
    bool writePage(uint32_t address, const uint8_t* data, size_t length);
    bool write(uint32_t address, const uint8_t* data, size_t length);
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
