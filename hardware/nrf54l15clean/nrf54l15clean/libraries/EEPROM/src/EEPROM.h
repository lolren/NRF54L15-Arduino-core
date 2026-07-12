/*
 * EEPROM.h - Arduino EEPROM compatibility library.
 * Original copyright (c) 2006 David A. Mellis. All rights reserved.
 * New version by Christopher Andrews, 2015.
 * Copyright (c) 2014 Ivan Grokhotkov. All rights reserved.
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * ESP flash emulation was ported by Paolo Becchi, modified by Elochukwu
 * Ifediora, and converted to NVS by lbernstone@gmail.com.
 *
 * Modified for the nRF54 Arduino Core in 2026: combined the reference/get/put
 * compatibility API with a CRC-protected nRF54 RRAM backend. See
 * ../../../LICENSES/LGPL-2.1-or-later.txt.
 */

#ifndef EEPROM_h
#define EEPROM_h

#include <Arduino.h>

class EEPROMClass;

class EEPROMRef {
public:
    EEPROMRef(EEPROMClass* eeprom, int index);

    EEPROMRef& operator=(uint8_t value);
    EEPROMRef& operator=(const EEPROMRef& other);
    operator uint8_t() const;
    EEPROMRef& update(uint8_t value);

private:
    EEPROMClass* _eeprom;
    int _index;
};

class EEPROMClass {
public:
    static constexpr size_t kMaxLength = 1024U;

    EEPROMClass();

    bool begin(size_t size = kMaxLength);
    void end();
    bool commit();

    uint8_t read(int address);
    void write(int address, uint8_t value);
    void update(int address, uint8_t value);

    template <typename T>
    T& get(int address, T& value) {
        if (!ensureStarted()) {
            return value;
        }
        if (address < 0) {
            return value;
        }

        const size_t addr = static_cast<size_t>(address);
        if ((addr + sizeof(T)) > _size) {
            return value;
        }

        memcpy(&value, &_buffer[addr], sizeof(T));
        return value;
    }

    template <typename T>
    const T& put(int address, const T& value) {
        if (!ensureStarted()) {
            return value;
        }
        if (address < 0) {
            return value;
        }

        const size_t addr = static_cast<size_t>(address);
        if ((addr + sizeof(T)) > _size) {
            return value;
        }

        const uint8_t* src = reinterpret_cast<const uint8_t*>(&value);
        bool changed = false;
        for (size_t i = 0; i < sizeof(T); ++i) {
            const size_t index = addr + i;
            if (_buffer[index] != src[i]) {
                _buffer[index] = src[i];
                changed = true;
            }
        }

        if (changed) {
            _dirty = true;
            if (_autoCommit) {
                (void)commit();
            }
        }

        return value;
    }

    size_t length() const;
    uint8_t* getDataPtr();

    EEPROMRef operator[](int address);

private:
    bool beginInternal(size_t size, bool autoCommit);
    bool ensureStarted();
    bool addressInRange(int address) const;

    bool _started;
    bool _dirty;
    bool _autoCommit;
    size_t _size;
    uint8_t _buffer[kMaxLength];
};

extern EEPROMClass EEPROM;

#endif
