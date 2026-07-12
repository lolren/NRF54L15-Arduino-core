/**************************************************************************/
/*!
 * @file debug.h
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
 * Modified for the nRF54 Arduino Core in 2026: reduced the declaration surface
 * to the heap/version diagnostics implemented by this bare-metal core.
 */
/**************************************************************************/
#ifndef NRF54L15_CLEAN_DEBUG_H_
#define NRF54L15_CLEAN_DEBUG_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int dbgHeapTotal(void);
int dbgHeapUsed(void);

static inline int dbgHeapFree(void)
{
    return dbgHeapTotal() - dbgHeapUsed();
}

void dbgMemInfo(void);
void dbgPrintVersion(void);
uint32_t getFreeHeapSize(void);

#ifdef __cplusplus
}
#endif

#endif  // NRF54L15_CLEAN_DEBUG_H_
