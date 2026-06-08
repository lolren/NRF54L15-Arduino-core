#pragma once
#include <Arduino.h>
#include <system/SystemLayer.h>
#include <lib/support/IntrusiveList.h>
#include <lib/support/ObjectLifeCycle.h>
#include <lib/support/CodeUtils.h>

namespace chip {
namespace System {

class LayerImplArduino : public Layer
{
public:
    LayerImplArduino() : mLayerState(), mTimerList(), mWorkList(), mWorkScheduled(false) {}
    ~LayerImplArduino() override { VerifyOrDie(mLayerState.Destroy()); }

    CHIP_ERROR Init() override {
        VerifyOrReturnError(mLayerState.SetInitializing(), CHIP_ERROR_INCORRECT_STATE);
        mLayerState.SetInitialized();
        return CHIP_NO_ERROR;
    }

    void Shutdown() override {
        while (!mTimerList.Empty()) {
            TimerNode * t = mTimerList.Head();
            mTimerList.PopFront();
            delete t;
        }
        while (!mWorkList.Empty()) {
            TimerNode * w = mWorkList.Head();
            mWorkList.PopFront();
            delete w;
        }
        mWorkScheduled = false;
        mLayerState.ResetFromInitialized();
    }

    bool IsInitialized() const override { return mLayerState.IsInitialized(); }

    CHIP_ERROR HandleEvents() override {
        VerifyOrReturnError(mLayerState.IsInitialized(), CHIP_ERROR_INCORRECT_STATE);
        uint32_t now = millis();
        while (!mTimerList.Empty()) {
            TimerNode * timer = mTimerList.Head();
            if (timer->mFireTime <= now) {
                mTimerList.PopFront();
                timer->mCallback(this, timer->mAppState);
                delete timer;
            } else {
                break;
            }
        }
        if (mWorkScheduled) {
            mWorkScheduled = false;
            while (!mWorkList.Empty()) {
                TimerNode * work = mWorkList.Head();
                mWorkList.PopFront();
                work->mCallback(this, work->mAppState);
                delete work;
            }
        }
        return CHIP_NO_ERROR;
    }

    CHIP_ERROR NewTimer(TimerCompleteCallback func, void * arg, PlatformTimer ** ret) override {
        *ret = nullptr;
        return CHIP_ERROR_NOT_IMPLEMENTED;
    }

    void FreeTimer(PlatformTimer * timer) override {}

    CHIP_ERROR ScheduleWork(TimerCompleteCallback func, void * arg) override {
        TimerNode * node = new TimerNode();
        node->mCallback = func;
        node->mAppState = arg;
        node->mFireTime = 0;
        mWorkList.PushBack(node);
        mWorkScheduled = true;
        return CHIP_NO_ERROR;
    }

    const char * ErrorStr(CHIP_ERROR err) override {
        return "error";
    }

    CHIP_ERROR StartTimer(Clock::Timeout delay, TimerCompleteCallback onComplete, void * appState) override {
        TimerNode * node = new TimerNode();
        node->mCallback = onComplete;
        node->mAppState = appState;
        node->mFireTime = millis() + delay;
        InsertTimer(node);
        return CHIP_NO_ERROR;
    }

    CHIP_ERROR ExtendTimerTo(Clock::Timeout delay, TimerCompleteCallback onComplete, void * appState) override {
        for (auto * node = mTimerList.Head(); node != nullptr; node = node->IntrusiveNext()) {
            if (node->mCallback == onComplete && node->mAppState == appState) {
                node->mFireTime = millis() + delay;
                return CHIP_NO_ERROR;
            }
        }
        return CHIP_ERROR_NOT_FOUND;
    }

    bool IsTimerActive(TimerCompleteCallback onComplete, void * appState) override {
        for (auto * node = mTimerList.Head(); node != nullptr; node = node->IntrusiveNext()) {
            if (node->mCallback == onComplete && node->mAppState == appState) {
                return true;
            }
        }
        return false;
    }

    Clock::Timeout GetRemainingTime(TimerCompleteCallback onComplete, void * appState) override {
        for (auto * node = mTimerList.Head(); node != nullptr; node = node->IntrusiveNext()) {
            if (node->mCallback == onComplete && node->mAppState == appState) {
                uint32_t now = millis();
                if (node->mFireTime > now)
                    return node->mFireTime - now;
                return 0;
            }
        }
        return 0;
    }

    void CancelTimer(TimerCompleteCallback onComplete, void * appState) override {
        for (auto * node = mTimerList.Head(); node != nullptr; node = node->IntrusiveNext()) {
            if (node->mCallback == onComplete && node->mAppState == appState) {
                mTimerList.Remove(node);
                delete node;
                return;
            }
        }
    }

private:
    struct TimerNode : public chip::IntrusiveLinkedListIntr<TimerNode> {
        TimerCompleteCallback mCallback;
        void * mAppState;
        uint32_t mFireTime;
    };

    void InsertTimer(TimerNode * node) {
        for (auto * cur = mTimerList.Head(); cur != nullptr; cur = cur->IntrusiveNext()) {
            if (cur->mFireTime >= node->mFireTime) {
                mTimerList.InsertBefore(cur, node);
                return;
            }
        }
        mTimerList.PushBack(node);
    }

    ObjectLifeCycle mLayerState;
    chip::IntrusiveList<TimerNode> mTimerList;
    chip::IntrusiveList<TimerNode> mWorkList;
    bool mWorkScheduled;
};

using LayerImpl = LayerImplArduino;

} // namespace System
} // namespace chip
