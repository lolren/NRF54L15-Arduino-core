#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <utility>

#include <inet/IPAddress.h>
#include <inet/UDPEndPoint.h>
#include <lib/support/CodeUtils.h>
#include <lib/support/ObjectLifeCycle.h>
#include <system/SystemLayer.h>
#include <system/SystemPacketBuffer.h>

#include "nrf54_thread_experimental.h"

namespace chip {
namespace Inet {

class InetLayer;

class UDPEndPointArduino : public UDPEndPoint
{
public:
    UDPEndPointArduino() = default;
    ~UDPEndPointArduino() override {
        VerifyOrDie(Release() == CHIP_NO_ERROR);
    }

    void Reset(xiao_nrf54l15::Nrf54ThreadExperimental * thread,
               uint16_t ephemeralPort, UDPEndPointArduino * endpointPool,
               size_t endpointPoolSize) {
        VerifyOrDie(!mAllocated);
        mThread = thread;
        mEphemeralPort = ephemeralPort;
        mEndpointPool = endpointPool;
        mEndpointPoolSize = endpointPoolSize;
        mAllocated = true;
        mLastCloseError = CHIP_NO_ERROR;
    }

    CHIP_ERROR Bind(const IPAddress & address, uint16_t port) {
        VerifyOrReturnError(mAllocated && mThread != nullptr,
                            CHIP_ERROR_INCORRECT_STATE);
        VerifyOrReturnError(port != 0U, CHIP_ERROR_INVALID_ARGUMENT);
        VerifyOrReturnError(address == IPAddress::Any,
                            CHIP_ERROR_NOT_IMPLEMENTED);
        if (mBound && mLocalPort == port) return CHIP_NO_ERROR;
        VerifyOrReturnError(!PortClaimedByPeer(port), CHIP_ERROR_BUSY);
        if (mBound) ReturnErrorOnFailure(CloseWithStatus());

        mLocalAddress = address;
        mLocalPort = port;
        if (!mThread->openUdp(port, HandleUdpReceive, this)) {
            mLocalAddress = IPAddress();
            mLocalPort = 0U;
            return CHIP_ERROR_INTERNAL;
        }
        mBound = true;
        return CHIP_NO_ERROR;
    }

    CHIP_ERROR BindUnspecifiedPort(const IPAddress & address) override {
        VerifyOrReturnError(mAllocated, CHIP_ERROR_INCORRECT_STATE);
        VerifyOrReturnError(mEphemeralPort != 0U, CHIP_ERROR_INCORRECT_STATE);
        return Bind(address, mEphemeralPort);
    }

    CHIP_ERROR Listen() {
        VerifyOrReturnError(mAllocated && mBound,
                            CHIP_ERROR_INCORRECT_STATE);
        mListening = true;
        return CHIP_NO_ERROR;
    }

    CHIP_ERROR SendTo(const IPAddress & address, uint16_t port,
                      System::PacketBufferHandle && message) override {
        VerifyOrReturnError(mAllocated && mThread != nullptr && mBound,
                            CHIP_ERROR_INCORRECT_STATE);
        VerifyOrReturnError(port != 0U && !message.IsNull(),
                            CHIP_ERROR_INVALID_ARGUMENT);
        VerifyOrReturnError(address != IPAddress::Any,
                            CHIP_ERROR_INVALID_ADDRESS);

        const size_t totalLength = message->TotalLength();
        VerifyOrReturnError(totalLength > 0U && totalLength <= sizeof(mTxBuffer),
                            CHIP_ERROR_MESSAGE_TOO_LONG);

        size_t offset = 0U;
        System::PacketBufferHandle cursor = message.Retain();
        while (!cursor.IsNull()) {
            const size_t length = cursor->DataLength();
            VerifyOrReturnError(length <= (sizeof(mTxBuffer) - offset),
                                CHIP_ERROR_MESSAGE_TOO_LONG);
            if (length > 0U) memcpy(mTxBuffer + offset, cursor->Start(), length);
            offset += length;
            if (!cursor->HasChainedBuffer()) break;
            cursor.Advance();
        }
        VerifyOrReturnError(offset == totalLength, CHIP_ERROR_INTERNAL);

        otIp6Address peerAddress = {};
        memcpy(peerAddress.mFields.m8, address.mAddr, sizeof(address.mAddr));
        return mThread->sendUdpFrom(mLocalPort, peerAddress, port, mTxBuffer,
                                    static_cast<uint16_t>(offset))
            ? CHIP_NO_ERROR
            : CHIP_ERROR_INTERNAL;
    }

    void Close() override { (void) CloseWithStatus(); }

    CHIP_ERROR CloseWithStatus() {
        if (mBound) {
            if (mThread == nullptr || mLocalPort == 0U) {
                mLastCloseError = CHIP_ERROR_INCORRECT_STATE;
                return mLastCloseError;
            }
            if (!mThread->closeUdp(mLocalPort)) {
                mLastCloseError = CHIP_ERROR_INTERNAL;
                return mLastCloseError;
            }
        }
        ClearClosedState();
        mLastCloseError = CHIP_NO_ERROR;
        return CHIP_NO_ERROR;
    }

    void SetReceiveCallback(ReceiveCallbackFunct callback,
                            void * appState) override {
        mReceiveCallback = callback;
        mAppState = appState;
    }

    void SetUDPEndPointAppState(void * appState) { mAppState = appState; }
    void * GetUDPEndPointAppState() const { return mAppState; }
    uint16_t GetBoundPort() const { return mLocalPort; }
    bool IsBound() const { return mBound; }
    bool IsListening() const { return mListening; }
    CHIP_ERROR GetLastCloseError() const { return mLastCloseError; }

private:
    friend class InetLayer;

    CHIP_ERROR Release() {
        ReturnErrorOnFailure(CloseWithStatus());
        mThread = nullptr;
        mEndpointPool = nullptr;
        mEndpointPoolSize = 0U;
        mEphemeralPort = 0U;
        mAllocated = false;
        return CHIP_NO_ERROR;
    }

    void ClearClosedState() {
        mLocalAddress = IPAddress();
        mLocalPort = 0U;
        mBound = false;
        mListening = false;
        mReceiveCallback = nullptr;
        mAppState = nullptr;
    }

    bool PortClaimedByPeer(uint16_t port) const {
        if (mEndpointPool == nullptr) return false;
        for (size_t i = 0U; i < mEndpointPoolSize; ++i) {
            const UDPEndPointArduino * endpoint = &mEndpointPool[i];
            if (endpoint != this && endpoint->mAllocated && endpoint->mBound &&
                endpoint->mLocalPort == port) {
                return true;
            }
        }
        return false;
    }

    static void HandleUdpReceive(void * context, const uint8_t * payload,
                                 uint16_t length,
                                 const otMessageInfo & messageInfo) {
        if (context == nullptr) return;
        static_cast<UDPEndPointArduino *>(context)->HandleUdpReceive(
            payload, length, messageInfo);
    }

    void HandleUdpReceive(const uint8_t * payload, uint16_t length,
                          const otMessageInfo & messageInfo) {
        if (!mListening || mReceiveCallback == nullptr ||
            (payload == nullptr && length > 0U)) {
            return;
        }

        System::PacketBufferHandle message = length == 0U
            ? System::PacketBufferHandle::New(0U)
            : System::PacketBufferHandle::NewWithData(payload, length);
        if (message.IsNull()) return;

        IPPacketInfo packetInfo = {};
        memcpy(packetInfo.mAddress.mAddr, messageInfo.mPeerAddr.mFields.m8,
               sizeof(packetInfo.mAddress.mAddr));
        packetInfo.mPort = messageInfo.mPeerPort;
        mReceiveCallback(this, std::move(message), packetInfo);
    }

    // The IPv6 minimum MTU includes the 40-byte IPv6 and 8-byte UDP headers.
    static constexpr size_t kMaxUdpPayload = 1280U - 40U - 8U;
    xiao_nrf54l15::Nrf54ThreadExperimental * mThread = nullptr;
    UDPEndPointArduino * mEndpointPool = nullptr;
    size_t mEndpointPoolSize = 0U;
    ReceiveCallbackFunct mReceiveCallback = nullptr;
    void * mAppState = nullptr;
    IPAddress mLocalAddress = {};
    uint16_t mLocalPort = 0U;
    uint16_t mEphemeralPort = 0U;
    bool mBound = false;
    bool mListening = false;
    bool mAllocated = false;
    CHIP_ERROR mLastCloseError = CHIP_NO_ERROR;
    uint8_t mTxBuffer[kMaxUdpPayload] = {0};
};

class InetLayer
{
public:
    InetLayer() = default;
    ~InetLayer() { VerifyOrDie(mLayerState.Destroy()); }

    void SetThreadTransport(
        xiao_nrf54l15::Nrf54ThreadExperimental & thread) {
        mThread = &thread;
    }

    CHIP_ERROR Init(System::Layer & systemLayer) {
        VerifyOrReturnError(mLayerState.SetInitializing(),
                            CHIP_ERROR_INCORRECT_STATE);
        VerifyOrReturnError(systemLayer.IsInitialized(),
                            CHIP_ERROR_INCORRECT_STATE);
        mSystemLayer = &systemLayer;
        mLayerState.SetInitialized();
        mLastShutdownError = CHIP_NO_ERROR;
        return CHIP_NO_ERROR;
    }

    void Shutdown() { (void) ShutdownWithStatus(); }

    CHIP_ERROR ShutdownWithStatus() {
        if (!mLayerState.IsInitialized()) {
            mLastShutdownError = CHIP_ERROR_INCORRECT_STATE;
            return mLastShutdownError;
        }
        for (size_t i = 0U; i < kMaxUDPEndpoints; ++i) {
            if (!mEndpointUsed[i]) continue;
            const CHIP_ERROR error = mEndpointPool[i].Release();
            if (error != CHIP_NO_ERROR) {
                mLastShutdownError = error;
                return error;
            }
            mEndpointUsed[i] = false;
        }
        if (!mLayerState.ResetFromInitialized()) {
            mLastShutdownError = CHIP_ERROR_INCORRECT_STATE;
            return mLastShutdownError;
        }
        mSystemLayer = nullptr;
        mLastShutdownError = CHIP_NO_ERROR;
        return CHIP_NO_ERROR;
    }

    bool IsInitialized() const { return mLayerState.IsInitialized(); }
    CHIP_ERROR GetLastShutdownError() const { return mLastShutdownError; }
    System::Layer & SystemLayer() const { return *mSystemLayer; }

    void Service() {
        if (mThread != nullptr) mThread->process();
    }

    CHIP_ERROR NewUDPEndPoint(UDPEndPointArduino ** result) {
        VerifyOrReturnError(mLayerState.IsInitialized(),
                            CHIP_ERROR_INCORRECT_STATE);
        VerifyOrReturnError(mThread != nullptr, CHIP_ERROR_INCORRECT_STATE);
        VerifyOrReturnError(result != nullptr, CHIP_ERROR_INVALID_ARGUMENT);
        *result = nullptr;
        for (size_t i = 0U; i < kMaxUDPEndpoints; ++i) {
            if (mEndpointUsed[i]) continue;
            mEndpointUsed[i] = true;
            mEndpointPool[i].Reset(
                mThread, static_cast<uint16_t>(kFirstEphemeralPort + i),
                mEndpointPool, kMaxUDPEndpoints);
            *result = &mEndpointPool[i];
            return CHIP_NO_ERROR;
        }
        return CHIP_ERROR_ENDPOINT_POOL_FULL;
    }

    void DeleteUDPEndPoint(UDPEndPointArduino * endpoint) {
        (void) DeleteUDPEndPointWithStatus(endpoint);
    }

    CHIP_ERROR DeleteUDPEndPointWithStatus(
        UDPEndPointArduino * endpoint) {
        VerifyOrReturnError(endpoint != nullptr, CHIP_ERROR_INVALID_ARGUMENT);
        for (size_t i = 0U; i < kMaxUDPEndpoints; ++i) {
            if (&mEndpointPool[i] != endpoint) continue;
            ReturnErrorOnFailure(endpoint->Release());
            mEndpointUsed[i] = false;
            return CHIP_NO_ERROR;
        }
        return CHIP_ERROR_INVALID_ARGUMENT;
    }

private:
    static constexpr size_t kMaxUDPEndpoints = 4U;
    static constexpr uint16_t kFirstEphemeralPort = 49152U;
    UDPEndPointArduino mEndpointPool[kMaxUDPEndpoints];
    bool mEndpointUsed[kMaxUDPEndpoints] = {};
    ObjectLifeCycle mLayerState;
    System::Layer * mSystemLayer = nullptr;
    xiao_nrf54l15::Nrf54ThreadExperimental * mThread = nullptr;
    CHIP_ERROR mLastShutdownError = CHIP_NO_ERROR;
};

}  // namespace Inet
}  // namespace chip
