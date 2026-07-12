/*
 * Copyright (c) 2019-2026 Nordic Semiconductor ASA
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Adapted for the nRF54 Arduino Core in 2026.
 */

#include "nrfx_temp.h"

#include "nrf54l15.h"

namespace {
nrfx_temp_handler_t g_temp_handler = nullptr;
bool g_temp_initialized = false;
int32_t g_last_temp_qc = 0;

#ifdef NRF_TRUSTZONE_NONSECURE
static constexpr uintptr_t kTempBase = 0x400D7000UL;
#else
static constexpr uintptr_t kTempBase = 0x500D7000UL;
#endif

NRF_TEMP_Type* tempPeripheral() {
  return reinterpret_cast<NRF_TEMP_Type*>(kTempBase);
}
}  // namespace

extern "C" nrfx_err_t nrfx_temp_init(const nrfx_temp_config_t* config,
                                     nrfx_temp_handler_t handler) {
  (void)config;
  g_temp_handler = handler;
  g_temp_initialized = true;
  g_last_temp_qc = 0;
  return NRFX_SUCCESS;
}

extern "C" void nrfx_temp_uninit(void) {
  g_temp_handler = nullptr;
  g_temp_initialized = false;
  g_last_temp_qc = 0;
}

extern "C" void nrfx_temp_measure(void) {
  if (!g_temp_initialized) {
    return;
  }

  NRF_TEMP_Type* const temp = tempPeripheral();
  temp->EVENTS_DATARDY = 0U;
  temp->TASKS_START = TEMP_TASKS_START_TASKS_START_Trigger;
  for (uint32_t spin = 0U; spin < 200000UL; ++spin) {
    if (temp->EVENTS_DATARDY != 0U) {
      break;
    }
  }
  temp->TASKS_STOP = TEMP_TASKS_STOP_TASKS_STOP_Trigger;
  if (temp->EVENTS_DATARDY == 0U) {
    return;
  }

  g_last_temp_qc = static_cast<int32_t>(temp->TEMP);
  temp->EVENTS_DATARDY = 0U;
  if (g_temp_handler != nullptr) {
    g_temp_handler(g_last_temp_qc);
  }
}

extern "C" int32_t nrfx_temp_result_get(void) { return g_last_temp_qc; }
