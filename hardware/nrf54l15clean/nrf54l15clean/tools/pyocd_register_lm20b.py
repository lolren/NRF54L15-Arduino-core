# Standalone script to register nRF54LM20A target with working flash algo.
# Pass to pyocd via --script: pyocd flash --script pyocd_register_lm20b.py -t nrf54lm20a ...
# No file writes, no admin rights needed. Works on Windows/Linux/Mac.

import pyocd.target.builtin
from pyocd.core.memory_map import FlashRegion, RamRegion, MemoryMap
from pyocd.debug.svd.loader import SVDFile
from pyocd.target.family.target_nRF54L import NRF54L

FLASH_ALGO = {
    'load_address': 0x20000000,
    'instructions': [
        0xE00ABE00,
        0xf8d24a02, 0x2b013400, 0x4770d1fb, 0x5004E000, 0x47702000, 0x47702000, 0x49072001, 0xf8c1b508,
        0xf7ff0500, 0xf8c1ffed, 0x20000540, 0xffe8f7ff, 0x0500f8c1, 0xbf00bd08, 0x5004E000, 0x2301b508,
        0xf8c14906, 0xf7ff3500, 0xf04fffdb, 0x600333ff, 0xf7ff2000, 0xf8c1ffd5, 0xbd080500, 0x5004E000,
        0x2301b538, 0x4d0c4614, 0x0103f021, 0x3500f8c5, 0xffc6f7ff, 0x44214622, 0x42911b00, 0x2000d105,
        0xffbef7ff, 0x0500f8c5, 0x4613bd38, 0x4b04f853, 0x461a5014, 0xbf00e7f1, 0x5004E000, 0x00000000
    ],
    'pc_init': 0x20000015, 'pc_unInit': 0x20000019,
    'pc_program_page': 0x20000065, 'pc_erase_sector': 0x20000019, 'pc_eraseAll': 0x2000001d,
    'static_base': 0x200000a4, 'begin_stack': 0x20000300, 'begin_data': 0x20001000,
    'page_size': 4, 'page_buffers': [0x20001000, 0x20001004], 'min_program_length': 4,
    'flash_start': 0, 'flash_size': 0x1FD000,
    'sector_sizes': ((0, 0x1FD000),)
}

class PatchedLM20A(NRF54L):
    MEMORY_MAP = MemoryMap(
        FlashRegion(start=0, length=0x001FD000, blocksize=0x1000, is_boot_memory=True, algo=FLASH_ALGO),
        FlashRegion(start=0x00FFD000, length=0x1000, blocksize=4, is_testable=False, is_erasable=False, algo=FLASH_ALGO),
        RamRegion(start=0x20000000, length=0x80000),
    )
    def __init__(self, session):
        super().__init__(session, self.MEMORY_MAP)
        self._svd_location = SVDFile.from_builtin("nrf54l15.svd")
    def check_flash_security(self):
        import logging; L = logging.getLogger(__name__)
        tid = self.dp.read_dp(0x24)
        if tid & 0xFFF != 0x289: L.error("Not a Nordic device!")
        if (tid & 0xF0000) not in (0xC0000, 0x00000): L.error("Not an nRF54L device!")
        if not self.ap_is_enabled():
            if self.session.options.get('auto_unlock'):
                L.warning("%s APPROTECT: mass erase", self.part_number)
                self.mass_erase()
        else: L.warning("%s not in secure state", self.part_number)

pyocd.target.builtin.BUILTIN_TARGETS['nrf54lm20a'] = PatchedLM20A
