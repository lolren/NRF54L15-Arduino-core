# Runtime pyOCD target hook for XIAO nRF54LM20A.
#
# Pass with:
#   pyocd load --script pyocd_register_lm20b.py -t nrf54lm20a ...
#
# The bundled host tools may carry pyOCD 0.42, which does not always include a
# native nrf54lm20a target. This script registers the target without modifying
# the user's pyOCD installation, which is important on Windows and managed
# Python environments.

from pyocd.core.memory_map import FlashRegion, RamRegion, MemoryMap
from pyocd.debug.svd.loader import SVDFile
from pyocd.target.family.target_nRF54L import NRF54L
import pyocd.target.builtin


FLASH_ALGO = {
    "load_address": 0x20000000,
    "instructions": [
        0xE00ABE00,
        0xF8D24A02, 0x2B013400, 0x4770D1FB, 0x5004E000, 0x47702000,
        0x47702000, 0x49072001, 0xF8C1B508, 0xF7FF0500, 0xF8C1FFED,
        0x20000540, 0xFFE8F7FF, 0x0500F8C1, 0xBF00BD08, 0x5004E000,
        0x2301B508, 0xF8C14906, 0xF7FF3500, 0xF04FFFDB, 0x600333FF,
        0xF7FF2000, 0xF8C1FFD5, 0xBD080500, 0x5004E000, 0x2301B538,
        0x4D0C4614, 0x0103F021, 0x3500F8C5, 0xFFC6F7FF, 0x44214622,
        0x42911B00, 0x2000D105, 0xFFBEF7FF, 0x0500F8C5, 0x4613BD38,
        0x4B04F853, 0x461A5014, 0xBF00E7F1, 0x5004E000, 0x00000000,
    ],
    "pc_init": 0x20000015,
    "pc_unInit": 0x20000019,
    "pc_program_page": 0x20000065,
    "pc_erase_sector": 0x20000019,
    "pc_eraseAll": 0x2000001D,
    "static_base": 0x200000A4,
    "begin_stack": 0x20000300,
    "begin_data": 0x20001000,
    "page_size": 4,
    "analyzer_supported": False,
    "analyzer_address": 0,
    "page_buffers": [0x20001000, 0x20001004],
    "min_program_length": 4,
    "ro_start": 4,
    "ro_size": 0xA0,
    "rw_start": 0xA4,
    "rw_size": 0,
    "zi_start": 0xA4,
    "zi_size": 0,
    "flash_start": 0x0,
    "flash_size": 0x1FD000,
    # Treat application NVM as one erase region. Sector erase on LM20A is not
    # reliable with older pyOCD algorithms; chip erase plus program is stable.
    "sector_sizes": ((0x0, 0x1FD000),),
}


class NRF54LM20A(NRF54L):
    """nRF54LM20A target: 2036 KB NVM, 512 KB RAM."""

    MEMORY_MAP = MemoryMap(
        FlashRegion(
            start=0x0,
            length=0x001FD000,
            blocksize=0x1000,
            is_boot_memory=True,
            algo=FLASH_ALGO,
        ),
        FlashRegion(
            start=0x00FFD000,
            length=0x1000,
            blocksize=0x4,
            is_testable=False,
            is_erasable=False,
            algo=FLASH_ALGO,
        ),
        RamRegion(start=0x20000000, length=0x80000),
    )

    def __init__(self, session):
        super().__init__(session, self.MEMORY_MAP)
        self._svd_location = SVDFile.from_builtin("nrf54l15.svd")


def _patch_or_register_target() -> None:
    target_cls = pyocd.target.builtin.BUILTIN_TARGETS.get("nrf54lm20a")
    if target_cls is None:
        pyocd.target.builtin.BUILTIN_TARGETS["nrf54lm20a"] = NRF54LM20A
        return

    for region in target_cls.MEMORY_MAP.regions:
        if getattr(region, "is_boot_memory", False):
            region.algo = FLASH_ALGO
            break


_patch_or_register_target()
