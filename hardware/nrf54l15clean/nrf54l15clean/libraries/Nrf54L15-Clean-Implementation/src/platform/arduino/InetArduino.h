#pragma once
#include <Arduino.h>
#include <system/SystemLayer.h>
#include <system/SystemPacketBuffer.h>
#include <lib/support/ObjectLifeCycle.h>
#include <lib/support/CodeUtils.h>
#include <inet/IPAddress.h>
#include <inet/UDPEndPoint.h>

namespace chip {
namespace Inet {

class UDPEndPointArduino : public UDPEndPoint
{
public:
    UDPEndPointArduino() : mAppState(nullptr) {}
    ~UDPEndPointArduino() { Close(); }

    CHIP_ERROR Bind(const IPAddress & /*addr*/, uint16_t /*port*/) { return CHIP_NO_ERROR; }
    CHIP_ERROR Listen() { return CHIP_NO_ERROR; }
    
    CHIP_ERROR SendTo(const IPAddress & /*addr*/, uint16_t /*port*/, System::PacketBufferHandle && /*msg*/) override
    {
        return CHIP_ERROR_NOT_IMPLEMENTED;
    }
    
    void Close() override {}
    void SetUDPEndPointAppState(void * appState) { mAppState = appState; }
    void * GetUDPEndPointAppState() const { return mAppState; }

private:
    void * mAppState;
};

class InetLayer
{
public:
    InetLayer() : mLayerState(), mSystemLayer(nullptr) {}
    ~InetLayer() { VerifyOrDie(mLayerState.Destroy()); }

    CHIP_ERROR Init(System::Layer & systemLayer) {
        VerifyOrReturnError(mLayerState.SetInitializing(), CHIP_ERROR_INCORRECT_STATE);
        VerifyOrReturnError(systemLayer.IsInitialized(), CHIP_ERROR_INCORRECT_STATE);
        mSystemLayer = &systemLayer;
        mLayerState.SetInitialized();
        return CHIP_NO_ERROR;
    }

    void Shutdown() { mLayerState.ResetFromInitialized(); mSystemLayer = nullptr; }
    bool IsInitialized() const { return mLayerState.IsInitialized(); }
    System::Layer & SystemLayer() const { return *mSystemLayer; }

    CHIP_ERROR NewUDPEndPoint(UDPEndPointArduino ** retEndPoint);
    void DeleteUDPEndPoint(UDPEndPointArduino * endPoint);

private:
    static constexpr size_t kMaxUDPEndpoints = 4;
    UDPEndPointArduino mEndpointPool[kMaxUDPEndpoints];
    bool mEndpointUsed[kMaxUDPEndpoints] = {};
    ObjectLifeCycle mLayerState;
    System::Layer * mSystemLayer;
};

inline CHIP_ERROR InetLayer::NewUDPEndPoint(UDPEndPointArduino ** retEndPoint)
{
    VerifyOrReturnError(mLayerState.IsInitialized(), CHIP_ERROR_INCORRECT_STATE);
    for (size_t i = 0; i < kMaxUDPEndpoints; i++) {
        if (!mEndpointUsed[i]) {
            mEndpointUsed[i] = true;
            *retEndPoint = &mEndpointPool[i];
            return CHIP_NO_ERROR;
        }
    }
    return CHIP_ERROR_ENDPOINT_POOL_FULL;
}

inline void InetLayer::DeleteUDPEndPoint(UDPEndPointArduino * endPoint)
{
    for (size_t i = 0; i < kMaxUDPEndpoints; i++) {
        if (&mEndpointPool[i] == endPoint) {
            mEndpointUsed[i] = false;
            endPoint->Close();
            return;
        }
    }
}

} // namespace Inet
} // namespace chip
