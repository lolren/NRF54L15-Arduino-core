#pragma once
#include <system/SystemError.h>
#include <system/SystemClock.h>
#include <system/SystemPacketBuffer.h>
#include <lib/support/IntrusiveList.h>
#include <lib/support/ObjectLifeCycle.h>
namespace chip { namespace System {
class Layer {
public:
    virtual ~Layer() {}
    virtual CHIP_ERROR Init() = 0;
    virtual void Shutdown() = 0;
    virtual bool IsInitialized() const = 0;
    virtual CHIP_ERROR HandleEvents() = 0;
    using TimerCompleteCallback = void (*)(Layer *, void *);
    class PlatformTimer {
    public:
        virtual CHIP_ERROR Arm(uint32_t usecs) = 0;
        virtual void Disarm() = 0;
        virtual ~PlatformTimer() {}
    };
    virtual CHIP_ERROR NewTimer(TimerCompleteCallback func, void * arg, PlatformTimer ** ret) = 0;
    virtual void FreeTimer(PlatformTimer * timer) = 0;
    virtual CHIP_ERROR ScheduleWork(TimerCompleteCallback func, void * arg) = 0;
    virtual const char * ErrorStr(CHIP_ERROR err) = 0;
    virtual CHIP_ERROR StartTimer(Clock::Timeout delay, TimerCompleteCallback onComplete, void * appState) = 0;
    virtual CHIP_ERROR ExtendTimerTo(Clock::Timeout delay, TimerCompleteCallback onComplete, void * appState) = 0;
    virtual bool IsTimerActive(TimerCompleteCallback onComplete, void * appState) = 0;
    virtual Clock::Timeout GetRemainingTime(TimerCompleteCallback onComplete, void * appState) = 0;
    virtual void CancelTimer(TimerCompleteCallback onComplete, void * appState) = 0;
};
}}
