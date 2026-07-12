/*
 * Printable.h - Arduino interface for printing complex types.
 * Copyright (c) 2011 Adrian McEwen. All rights reserved.
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Modified for the nRF54 Arduino Core in 2026: modernized the destructor and
 * reformatted the interface. See ../../LICENSES/LGPL-2.1-or-later.txt.
 */

#ifndef Printable_h
#define Printable_h

#include <stddef.h>

class Print;

class Printable {
public:
    virtual ~Printable() = default;
    virtual size_t printTo(Print& p) const = 0;
};

#endif
