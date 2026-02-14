#ifndef Wire_h
#define Wire_h

#include <stddef.h>
#include <stdint.h>

struct i2c_target_config;

class TwoWire {
public:
    using ReceiveCallback = void (*)(int);
    using RequestCallback = void (*)();

    TwoWire();

    void begin();
    void begin(uint8_t address);
    void end();
    void setClock(uint32_t clockHz);
    void onReceive(ReceiveCallback callback);
    void onRequest(RequestCallback callback);

    void beginTransmission(uint8_t address);
    size_t write(uint8_t data);
    size_t write(const uint8_t *buffer, size_t size);
    int endTransmission(bool sendStop = true);

    uint8_t requestFrom(uint8_t address, uint8_t quantity, bool sendStop = true);

    int available();
    int read();
    int peek();
    void flush();

private:
    enum TargetDirection : uint8_t {
        TARGET_DIR_NONE = 0,
        TARGET_DIR_WRITE = 1,
        TARGET_DIR_READ = 2,
    };

    const void *_i2c;
    uint8_t _txAddress;
    uint8_t _txBuffer[256];
    uint8_t _rxBuffer[256];
    uint8_t _targetTxBuffer[256];
    size_t _txLength;
    size_t _rxLength;
    size_t _rxIndex;
    size_t _targetTxLength;
    size_t _targetTxIndex;
    int _peek;
    uint32_t _clockHz;
    uint8_t _targetAddress;
    bool _targetRegistered;
    bool _inOnRequestCallback;
    TargetDirection _targetDirection;
    ReceiveCallback _onReceive;
    RequestCallback _onRequest;

    bool isTargetWriteContext() const;
    void clearControllerTxState();
    void clearReceiveState();
    void clearTargetTxState();
    int provideTargetByte(uint8_t *value);

    int handleTargetWriteRequested();
    int handleTargetWriteReceived(uint8_t value);
    int handleTargetReadRequested(uint8_t *value);
    int handleTargetReadProcessed(uint8_t *value);
    int handleTargetStop();

    static int targetWriteRequestedAdapter(struct i2c_target_config *config);
    static int targetWriteReceivedAdapter(struct i2c_target_config *config, uint8_t value);
    static int targetReadRequestedAdapter(struct i2c_target_config *config, uint8_t *value);
    static int targetReadProcessedAdapter(struct i2c_target_config *config, uint8_t *value);
    static int targetStopAdapter(struct i2c_target_config *config);
};

extern TwoWire Wire;

#endif
