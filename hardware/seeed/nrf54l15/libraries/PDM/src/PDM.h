#ifndef PDM_H
#define PDM_H

#include <Arduino.h>

class PDMClass {
public:
    bool begin(uint32_t sampleRate = 16000U, uint8_t channels = 1U);
    void end();

    int available();
    int read(void *buffer, size_t bytes);

    bool isRunning() const;

private:
    const void *_dmic = nullptr;
    bool _running = false;

    void *_pendingBlock = nullptr;
    size_t _pendingSize = 0;
    size_t _pendingIndex = 0;
};

extern PDMClass PDM;

#endif
