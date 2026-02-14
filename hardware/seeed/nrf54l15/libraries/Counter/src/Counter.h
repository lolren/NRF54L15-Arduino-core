#ifndef COUNTER_H
#define COUNTER_H

#include <Arduino.h>

class CounterClass {
public:
    bool begin();
    bool start();
    bool stop();
    bool reset();
    uint32_t read();
    bool isRunning() const;

private:
    const void *_counter = nullptr;
    bool _running = false;
    bool _useSoftwareCounter = false;
    uint64_t _softBaseMs = 0;
    uint64_t _softStartMs = 0;
};

extern CounterClass Counter;

#endif
