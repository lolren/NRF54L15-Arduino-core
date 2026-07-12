/*
 * Server.h - Arduino Server base class.
 * Copyright (c) 2011 Adrian McEwen. All rights reserved.
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Modified for the nRF54 Arduino Core in 2026: reformatted for the local Print
 * API. See ../../LICENSES/LGPL-2.1-or-later.txt.
 */

#ifndef server_h
#define server_h

#include "Print.h"

class Server : public Print {
public:
    virtual void begin() = 0;
};

#endif
