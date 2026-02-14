#ifndef I2S_H
#define I2S_H

#include <Arduino.h>

class I2SClass {
public:
    bool begin(uint32_t sampleRate = 16000U, uint8_t bitsPerSample = 16U, uint8_t channels = 2U);
    void end();

    int write(const void *buffer, size_t bytes);
    int read(void *buffer, size_t bytes);

    bool isReady() const;

private:
    const void *_i2s = nullptr;
    bool _configured = false;
    bool _txStarted = false;
    bool _rxStarted = false;
};

extern I2SClass I2S;

#endif
