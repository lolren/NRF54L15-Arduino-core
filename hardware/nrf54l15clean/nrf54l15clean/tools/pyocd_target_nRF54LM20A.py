# pyOCD debugger
# Copyright (c) 2024-2026 Nordic Semiconductor ASA
# SPDX-License-Identifier: Apache-2.0

from ...core.memory_map import FlashRegion, RamRegion, MemoryMap
from ...debug.svd.loader import SVDFile
from ..family.target_nRF54L import NRF54L

FLASH_ALGO = {
    'load_address' : 0x20000000,

    # Flash algorithm adapted for nRF54LM20B (RRAMC at 0x5004E000 vs 0x5004B000 on L15)
    'instructions': [
    0xE00ABE00,
    0xf8d24a02, 0x2b013400, 0x4770d1fb, 0x5004E000, 0x47702000, 0x47702000, 0x49072001, 0xf8c1b508,
    0xf7ff0500, 0xf8c1ffed, 0x20000540, 0xffe8f7ff, 0x0500f8c1, 0xbf00bd08, 0x5004E000, 0x2301b508,
    0xf8c14906, 0xf7ff3500, 0xf04fffdb, 0x600333ff, 0xf7ff2000, 0xf8c1ffd5, 0xbd080500, 0x5004E000,
    0x2301b538, 0x4d0c4614, 0x0103f021, 0x3500f8c5, 0xffc6f7ff, 0x44214622, 0x42911b00, 0x2000d105,
    0xffbef7ff, 0x0500f8c5, 0x4613bd38, 0x4b04f853, 0x461a5014, 0xbf00e7f1, 0x5004E000, 0x00000000
    ],

    # Relative function addresses
    'pc_init': 0x20000015,
    'pc_unInit': 0x20000019,
    'pc_program_page': 0x20000065,
    'pc_erase_sector': 0x20000019,  # Patched: use pc_unInit (no-op) for LM20B
    'pc_eraseAll': 0x2000001d,

    'static_base' : 0x20000000 + 0x00000004 + 0x000000a0,
    'begin_stack' : 0x20000300,
    'begin_data' : 0x20000000 + 0x1000,
    'page_size' : 0x4,
    'analyzer_supported' : False,
    'analyzer_address' : 0x00000000,
    'page_buffers' : [0x20001000, 0x20001004],   # Enable double buffering
    'min_program_length' : 0x4,

    # Flash information for nRF54LM20A/B (2036 KB = 0x1FD000)
    # Note: sector-level erase via flash algo doesn't work on LM20B yet.
    # Use chip-level erase (pc_eraseAll) which works via CTRL-AP.
    'flash_start': 0x0,
    'flash_size': 0x1FD000,
    # Force chip erase for all operations (sector erase broken on LM20B)
    'sector_sizes': (
        (0x0, 0x1FD000),    # Whole flash as one "sector" → always chip erase
    )
}

class NRF54LM20A(NRF54L):
    """nRF54LM20A/B target - 2036KB NVM, 512KB RAM"""

    MEMORY_MAP = MemoryMap(
        FlashRegion(
            start=0x0,
            length=0x001FD000,    # 2036 KB
            blocksize=0x1000,
            is_boot_memory=True,
            algo=FLASH_ALGO,
        ),
        # User Information Configuration Registers (UICR) as a flash region
        FlashRegion(
            start=0x00FFD000,
            length=0x1000,
            blocksize=0x4,
            is_testable=False,
            is_erasable=False,
            algo=FLASH_ALGO,
        ),
        RamRegion(start=0x20000000, length=0x80000),  # 512 KB
    )

    def __init__(self, session):
        super(NRF54LM20A, self).__init__(session, self.MEMORY_MAP)
        # Use nrf54l15 SVD as best available approximation
        self._svd_location = SVDFile.from_builtin("nrf54l15.svd")
