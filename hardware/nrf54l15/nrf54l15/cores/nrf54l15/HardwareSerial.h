#ifndef HardwareSerial_h
#define HardwareSerial_h

#include <stdint.h>

#include "Stream.h"

struct device;

class HardwareSerial : public Stream {
public:
    explicit HardwareSerial(const struct device *uart = nullptr);

    void begin(unsigned long baud);
    void begin(unsigned long baud, uint16_t config);
    void end();

    int available() override;
    int read() override;
    int peek() override;
    void flush() override;

    size_t write(uint8_t value) override;
    using Print::write;

    operator bool() const;

private:
    const struct device *_uart;
    int _peek;
};

extern HardwareSerial Serial;
extern HardwareSerial Serial1;

#endif
