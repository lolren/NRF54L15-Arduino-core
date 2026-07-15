#pragma once
#include <Arduino.h>
#include <system/SystemLayer.h>
#include <lib/support/IntrusiveList.h>
#include <lib/support/ObjectLifeCycle.h>
#include <lib/support/CodeUtils.h>
#include <new>
#include <stdlib.h>

namespace chip {
namespace System {

class LayerImplArduino : public Layer
{
public:
    LayerImplArduino() : mLayerState(), mTimerList(), mWorkList(),
                         mWorkScheduled(false), mHandlingEvents(false) {}
    ~LayerImplArduino() override { VerifyOrDie(mLayerState.Destroy()); }

    CHIP_ERROR Init() override {
        VerifyOrReturnError(mLayerState.SetInitializing(), CHIP_ERROR_INCORRECT_STATE);
        mLayerState.SetInitialized();
        return CHIP_NO_ERROR;
    }

    void Shutdown() override {
        while (!mTimerList.Empty()) {
            auto it = mTimerList.begin();
            TimerNode * t = &(*it);
            mTimerList.Remove(t);
            DestroyTimerNode(t);
        }
        while (!mWorkList.Empty()) {
            auto it = mWorkList.begin();
            TimerNode * w = &(*it);
            mWorkList.Remove(w);
            DestroyTimerNode(w);
        }
        mWorkScheduled = false;
        mLayerState.ResetFromInitialized();
    }

    bool IsInitialized() const override { return mLayerState.IsInitialized(); }

    CHIP_ERROR HandleEvents() override {
        VerifyOrReturnError(mLayerState.IsInitialized(), CHIP_ERROR_INCORRECT_STATE);
        VerifyOrReturnError(!mHandlingEvents, CHIP_ERROR_INCORRECT_STATE);
        mHandlingEvents = true;

        // Restart the scan after every callback because callbacks may cancel
        // other timers. Timers/work created by a callback are deferred until
        // the next event pass, which prevents a self-rescheduling callback from
        // trapping the caller in HandleEvents().
        while (true) {
            TimerNode * expired = nullptr;
            const uint32_t now = millis();
            for (auto it = mTimerList.begin(); it != mTimerList.end(); ++it) {
                TimerNode * timer = &(*it);
                if (!timer->mDeferred && DeadlineReached(now, timer->mFireTime)) {
                    expired = timer;
                    break;
                }
            }
            if (expired == nullptr) break;

            const TimerCompleteCallback callback = expired->mCallback;
            void * appState = expired->mAppState;
            mTimerList.Remove(expired);
            DestroyTimerNode(expired);
            callback(this, appState);
        }

        if (mWorkScheduled) {
            while (true) {
                TimerNode * work = nullptr;
                for (auto it = mWorkList.begin(); it != mWorkList.end(); ++it) {
                    if (!it->mDeferred) {
                        work = &(*it);
                        break;
                    }
                }
                if (work == nullptr) break;
                const TimerCompleteCallback callback = work->mCallback;
                void * appState = work->mAppState;
                mWorkList.Remove(work);
                DestroyTimerNode(work);
                callback(this, appState);
            }
        }

        for (auto it = mTimerList.begin(); it != mTimerList.end(); ++it) {
            it->mDeferred = false;
        }
        for (auto it = mWorkList.begin(); it != mWorkList.end(); ++it) {
            it->mDeferred = false;
        }
        mWorkScheduled = !mWorkList.Empty();
        mHandlingEvents = false;
        return CHIP_NO_ERROR;
    }

    CHIP_ERROR NewTimer(TimerCompleteCallback func, void * arg, PlatformTimer ** ret) override {
        VerifyOrReturnError(mLayerState.IsInitialized(), CHIP_ERROR_INCORRECT_STATE);
        VerifyOrReturnError(func != nullptr && ret != nullptr, CHIP_ERROR_INVALID_ARGUMENT);
        *ret = AllocatePlatformTimer(*this, func, arg);
        return *ret != nullptr ? CHIP_NO_ERROR : CHIP_ERROR_NO_MEMORY;
    }

    void FreeTimer(PlatformTimer * timer) override {
        DestroyPlatformTimer(static_cast<ArduinoPlatformTimer *>(timer));
    }

    CHIP_ERROR ScheduleWork(TimerCompleteCallback func, void * arg) override {
        VerifyOrReturnError(mLayerState.IsInitialized(), CHIP_ERROR_INCORRECT_STATE);
        VerifyOrReturnError(func != nullptr, CHIP_ERROR_INVALID_ARGUMENT);
        TimerNode * node = AllocateTimerNode();
        VerifyOrReturnError(node != nullptr, CHIP_ERROR_NO_MEMORY);
        node->mCallback = func;
        node->mAppState = arg;
        node->mFireTime = 0;
        node->mDeferred = mHandlingEvents;
        mWorkList.PushBack(node);
        mWorkScheduled = true;
        return CHIP_NO_ERROR;
    }

    const char * ErrorStr(CHIP_ERROR err) override {
        return err == CHIP_NO_ERROR ? "no error" : "CHIP error";
    }

    CHIP_ERROR StartTimer(Clock::Timeout delay, TimerCompleteCallback onComplete, void * appState) override {
        VerifyOrReturnError(mLayerState.IsInitialized(), CHIP_ERROR_INCORRECT_STATE);
        VerifyOrReturnError(onComplete != nullptr, CHIP_ERROR_INVALID_ARGUMENT);
        VerifyOrReturnError(delay <= kMaxTimerDelayMs, CHIP_ERROR_INVALID_ARGUMENT);
        CancelTimer(onComplete, appState);
        TimerNode * node = AllocateTimerNode();
        VerifyOrReturnError(node != nullptr, CHIP_ERROR_NO_MEMORY);
        node->mCallback = onComplete;
        node->mAppState = appState;
        node->mFireTime = millis() + delay;
        node->mDeferred = mHandlingEvents;
        InsertTimer(node);
        return CHIP_NO_ERROR;
    }

    CHIP_ERROR ExtendTimerTo(Clock::Timeout delay, TimerCompleteCallback onComplete, void * appState) override {
        VerifyOrReturnError(mLayerState.IsInitialized(), CHIP_ERROR_INCORRECT_STATE);
        VerifyOrReturnError(delay > 0U && delay <= kMaxTimerDelayMs &&
                                onComplete != nullptr,
                            CHIP_ERROR_INVALID_ARGUMENT);
        for (auto it = mTimerList.begin(); it != mTimerList.end(); ++it) {
            TimerNode * node = &(*it);
            if (node->mCallback == onComplete && node->mAppState == appState) {
                if (RemainingTime(millis(), node->mFireTime) < delay) {
                    return StartTimer(delay, onComplete, appState);
                }
                return CHIP_NO_ERROR;
            }
        }
        return StartTimer(delay, onComplete, appState);
    }

    bool IsTimerActive(TimerCompleteCallback onComplete, void * appState) override {
        for (auto it = mTimerList.begin(); it != mTimerList.end(); ++it) {
            TimerNode * node = &(*it);
            if (node->mCallback == onComplete && node->mAppState == appState) {
                return true;
            }
        }
        return false;
    }

    Clock::Timeout GetRemainingTime(TimerCompleteCallback onComplete, void * appState) override {
        for (auto it = mTimerList.begin(); it != mTimerList.end(); ++it) {
            TimerNode * node = &(*it);
            if (node->mCallback == onComplete && node->mAppState == appState) {
                return RemainingTime(millis(), node->mFireTime);
            }
        }
        return 0;
    }

    void CancelTimer(TimerCompleteCallback onComplete, void * appState) override {
        for (auto it = mTimerList.begin(); it != mTimerList.end(); ) {
            TimerNode * node = &(*it);
            ++it;
            if (node->mCallback == onComplete && node->mAppState == appState) {
                mTimerList.Remove(node);
                DestroyTimerNode(node);
            }
        }
    }

private:
    struct TimerNode : public IntrusiveListNodeBase<> {
        TimerCompleteCallback mCallback;
        void * mAppState;
        uint32_t mFireTime;
        bool mDeferred;
    };

    class ArduinoPlatformTimer final : public PlatformTimer {
    public:
        ArduinoPlatformTimer(LayerImplArduino & owner,
                             TimerCompleteCallback callback, void * appState) :
            mOwner(owner), mCallback(callback), mAppState(appState), mArmed(false) {}

        ~ArduinoPlatformTimer() override { Disarm(); }

        CHIP_ERROR Arm(uint32_t usecs) override {
            Disarm();
            const uint32_t delayMs = static_cast<uint32_t>(
                (static_cast<uint64_t>(usecs) + 999ULL) / 1000ULL);
            CHIP_ERROR error = mOwner.StartTimer(delayMs, Fire, this);
            mArmed = (error == CHIP_NO_ERROR);
            return error;
        }

        void Disarm() override {
            if (!mArmed) return;
            mOwner.CancelTimer(Fire, this);
            mArmed = false;
        }

    private:
        static void Fire(Layer * layer, void * appState) {
            ArduinoPlatformTimer * timer =
                static_cast<ArduinoPlatformTimer *>(appState);
            timer->mArmed = false;
            timer->mCallback(layer, timer->mAppState);
        }

        LayerImplArduino & mOwner;
        TimerCompleteCallback mCallback;
        void * mAppState;
        bool mArmed;
    };

    static constexpr uint32_t kMaxTimerDelayMs = 0x7FFFFFFFUL;

    static bool DeadlineReached(uint32_t now, uint32_t deadline) {
        return static_cast<int32_t>(now - deadline) >= 0;
    }

    static uint32_t RemainingTime(uint32_t now, uint32_t deadline) {
        const int32_t remaining = static_cast<int32_t>(deadline - now);
        return remaining > 0 ? static_cast<uint32_t>(remaining) : 0U;
    }

    static TimerNode * AllocateTimerNode() {
        void * storage = malloc(sizeof(TimerNode));
        return storage != nullptr ? new (storage) TimerNode() : nullptr;
    }

    static void DestroyTimerNode(TimerNode * node) {
        if (node == nullptr) return;
        node->~TimerNode();
        free(node);
    }

    static ArduinoPlatformTimer * AllocatePlatformTimer(
        LayerImplArduino & owner, TimerCompleteCallback callback,
        void * appState) {
        void * storage = malloc(sizeof(ArduinoPlatformTimer));
        return storage != nullptr
            ? new (storage) ArduinoPlatformTimer(owner, callback, appState)
            : nullptr;
    }

    static void DestroyPlatformTimer(ArduinoPlatformTimer * timer) {
        if (timer == nullptr) return;
        timer->~ArduinoPlatformTimer();
        free(timer);
    }

    void InsertTimer(TimerNode * node) {
        // Timers are scanned rather than sorted so 32-bit millis() rollover
        // cannot invert absolute deadline ordering.
        mTimerList.PushBack(node);
    }

    ObjectLifeCycle mLayerState;
    IntrusiveList<TimerNode> mTimerList;
    IntrusiveList<TimerNode> mWorkList;
    bool mWorkScheduled;
    bool mHandlingEvents;
};

using LayerImpl = LayerImplArduino;

} // namespace System
} // namespace chip
