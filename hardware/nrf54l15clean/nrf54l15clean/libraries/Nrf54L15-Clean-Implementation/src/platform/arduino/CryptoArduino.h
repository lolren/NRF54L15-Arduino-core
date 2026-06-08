#pragma once
#include <Arduino.h>
#include <system/SystemError.h>
namespace chip { namespace Crypto {
    CHIP_ERROR GetDRBG(uint8_t *buf, size_t len) {
        for (size_t i = 0; i < len; i++) buf[i] = random(256);
        return CHIP_NO_ERROR;
    }
}}
