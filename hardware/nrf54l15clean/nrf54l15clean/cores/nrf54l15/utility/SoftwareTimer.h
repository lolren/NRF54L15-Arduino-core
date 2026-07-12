/**************************************************************************/
/*!
 * @file SoftwareTimer.h
 * @author hathach (tinyusb.org)
 *
 * Copyright (c) 2018 Adafruit Industries (adafruit.com)
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holders nor the names of its
 *    contributors may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS "AS IS" AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN
 * NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Modified for the nRF54 Arduino Core in 2026: replaced FreeRTOS timer handles
 * with a cooperative, interrupt-safe bare-metal timer list.
 */
/**************************************************************************/
#ifndef SOFTWARETIMER_H_
#define SOFTWARETIMER_H_

#include "Arduino.h"

class SoftwareTimer;

typedef SoftwareTimer* TimerHandle_t;
typedef void (*TimerCallbackFunction_t)(TimerHandle_t timer);

class SoftwareTimer {
 public:
  SoftwareTimer();
  virtual ~SoftwareTimer();

  void begin(uint32_t ms, TimerCallbackFunction_t callback, void* timerID = nullptr,
             bool repeating = true);
  TimerHandle_t getHandle(void) { return this; }

  void setID(void* id);
  void* getID(void);

  bool start(void);
  bool stop(void);
  bool reset(void);
  bool setPeriod(uint32_t ms);
  static void serviceAll();

 private:
  static SoftwareTimer* head_;

  SoftwareTimer* next_;
  uint32_t period_ms_;
  uint32_t next_fire_ms_;
  TimerCallbackFunction_t callback_;
  void* timer_id_;
  bool repeating_;
  bool active_;
  uint32_t service_epoch_;

  void serviceOne(uint32_t now_ms);
};

extern "C" void nrf54l15_software_timer_service(void);

#endif  // SOFTWARETIMER_H_
