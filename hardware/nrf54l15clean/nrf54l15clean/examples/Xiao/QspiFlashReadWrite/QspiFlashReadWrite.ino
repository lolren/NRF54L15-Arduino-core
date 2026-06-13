/*
 * QspiFlashReadWrite — PY25Q64 onboard flash on XIAO nRF54LM20A
 *
 * Read, sector erase (4 KB), and page write (up to 256 bytes)
 * via the dedicated HS-SPI / QSPI pads. The flash is on P2 pins,
 * not the standard XIAO SPI header.
 *
 * Flash commands (PY25Q64 datasheet):
 *   0x06  WREN   Write Enable — must precede each erase/write
 *   0x20  SE     Sector Erase (4 KB) — sets all bytes to 0xFF
 *   0x02  PP     Page Program (1–256 bytes)
 *   0x03  READ   Read Data
 *   0x05  RDSR   Read Status Register (bit 0 = BUSY)
 *   0xB9  DP     Deep Power Down
 *   0xAB  RDP    Release from Deep Power Down
 *
 * IMPORTANT: SPI_HS shares SPIM00 with SPI. Do not use both in the
 * same sketch simultaneously.
 */

#include <Arduino.h>
#include <SPI.h>

// --- Flash commands --------------------------------------------------------
#define CMD_WREN  0x06
#define CMD_RDSR  0x05
#define CMD_READ  0x03
#define CMD_SE    0x20
#define CMD_PP    0x02
#define CMD_RDID  0x9F
#define CMD_DP    0xB9
#define CMD_RDP   0xAB

// --- Pin: flash CS = P2.12 (HS-SPI SS pad) --------------------------------
#define FLASH_CS  47  // P2.05 = QSPI CS

// --- Low-level SPI helpers -------------------------------------------------
static void cs_low()  { digitalWrite(FLASH_CS, LOW); }
static void cs_high() { digitalWrite(FLASH_CS, HIGH); }

static void wren() {
    cs_low(); SPI_HS.transfer(CMD_WREN); cs_high();
}

static bool busy() {
    cs_low(); SPI_HS.transfer(CMD_RDSR);
    bool b = SPI_HS.transfer(0xFF) & 1; cs_high();
    return b;
}

static void wait_ready() { while (busy()) delayMicroseconds(10); }

// --- Public operations -----------------------------------------------------
static void flash_read(uint32_t addr, uint8_t* buf, size_t len) {
    cs_low();
    SPI_HS.transfer(CMD_READ);
    SPI_HS.transfer((addr >> 16) & 0xFF);
    SPI_HS.transfer((addr >> 8) & 0xFF);
    SPI_HS.transfer(addr & 0xFF);
    for (size_t i = 0; i < len; i++) buf[i] = SPI_HS.transfer(0xFF);
    cs_high();
}

static void flash_erase_sector(uint32_t addr) {
    wren();
    cs_low();
    SPI_HS.transfer(CMD_SE);
    SPI_HS.transfer((addr >> 16) & 0xFF);
    SPI_HS.transfer((addr >> 8) & 0xFF);
    SPI_HS.transfer(addr & 0xFF);
    cs_high();
    wait_ready();
}

static void flash_write_page(uint32_t addr, const uint8_t* data, size_t len) {
    wren();
    cs_low();
    SPI_HS.transfer(CMD_PP);
    SPI_HS.transfer((addr >> 16) & 0xFF);
    SPI_HS.transfer((addr >> 8) & 0xFF);
    SPI_HS.transfer(addr & 0xFF);
    for (size_t i = 0; i < len; i++) SPI_HS.transfer(data[i]);
    cs_high();
    wait_ready();
}

static void flash_deep_power_down() {
    cs_low(); SPI_HS.transfer(CMD_DP); cs_high();
}

// --- Setup ----------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    delay(250);

    pinMode(FLASH_CS, OUTPUT);
    digitalWrite(FLASH_CS, HIGH);

    SPI_HS.begin();
    SPI_HS.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));

    // ── Wake from deep power-down (previous sketch may have slept) ──
    cs_low();
    SPI_HS.transfer(CMD_RDP);   // Release from DP
    cs_high();
    delay(1);

    // ── 1. Identify the chip ─────────────────────────────────────────
    cs_low();
    SPI_HS.transfer(CMD_RDID);
    uint8_t j0 = SPI_HS.transfer(0xFF);
    uint8_t j1 = SPI_HS.transfer(0xFF);
    uint8_t j2 = SPI_HS.transfer(0xFF);
    cs_high();

    Serial.println();
    Serial.print("JEDEC ID: ");
    Serial.print(j0, HEX); Serial.print(' ');
    Serial.print(j1, HEX); Serial.print(' ');
    Serial.println(j2, HEX);

    // ── 2. Read first 32 bytes of sector 0 ────────────────────────────
    uint8_t buf[32] = {0};
    flash_read(0, buf, sizeof(buf));
    Serial.print("Before erase: ");
    for (int i = 0; i < 32; i++) { Serial.print(buf[i], HEX); Serial.print(' '); }
    Serial.println();

    // ── 3. Erase sector 0 (sets all bytes to 0xFF) ────────────────────
    Serial.println("Erasing sector 0 (4 KB)...");
    flash_erase_sector(0);

    flash_read(0, buf, sizeof(buf));
    Serial.print("After erase:  ");
    for (int i = 0; i < 32; i++) { Serial.print(buf[i], HEX); Serial.print(' '); }
    Serial.println();

    // ── 4. Write test pattern 0x00..0x1F ──────────────────────────────
    uint8_t test[32];
    for (int i = 0; i < 32; i++) test[i] = i;
    Serial.println("Writing 0x00..0x1F...");
    flash_write_page(0, test, 32);

    flash_read(0, buf, sizeof(buf));
    Serial.print("After write:  ");
    for (int i = 0; i < 32; i++) { Serial.print(buf[i], HEX); Serial.print(' '); }
    Serial.println();

    // ── 5. Put flash to sleep ─────────────────────────────────────────
    flash_deep_power_down();
    Serial.println("Flash in deep power-down.");

    SPI_HS.endTransaction();
    SPI_HS.end();
}

void loop() {}
