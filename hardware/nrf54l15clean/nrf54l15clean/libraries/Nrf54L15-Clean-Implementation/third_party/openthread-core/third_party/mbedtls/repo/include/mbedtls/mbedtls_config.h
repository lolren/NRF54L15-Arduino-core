/*
 * nRF54L15 Clean Arduino OpenThread mbedTLS config shim.
 *
 * The imported mbedTLS source is kept as upstream as possible. OpenThread
 * expects its own DTLS-sized configuration, so this public mbedTLS config
 * header forwards to the OpenThread-provided config one directory above the
 * imported repo.
 */
#pragma once

#include "../../../mbedtls-config.h"
