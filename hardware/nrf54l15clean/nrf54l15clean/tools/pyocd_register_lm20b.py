# Standalone script to patch nRF54LM20A flash algorithm at runtime.
# Pass to pyocd via --script: pyocd load --script pyocd_register_lm20b.py -t nrf54lm20a ...
# No file writes, no admin rights needed.
import pyocd.target.builtin

FIXED_ALGO = {
    'load_address': 0x20000000,
    'instructions': [
        0xE00ABE00,
        0xf8d24a02, 0x2b013400, 0x4770d1fb, 0x5004E000, 0x47702000, 0x47702000,
        0x49072001, 0xf8c1b508, 0xf7ff0500, 0xf8c1ffed, 0x20000540, 0xffe8f7ff,
        0x0500f8c1, 0xbf00bd08, 0x5004E000, 0x2301b508, 0xf8c14906, 0xf7ff3500,
        0xf04fffdb, 0x600333ff, 0xf7ff2000, 0xf8c1ffd5, 0xbd080500, 0x5004E000,
        0x2301b538, 0x4d0c4614, 0x0103f021, 0x3500f8c5, 0xffc6f7ff, 0x44214622,
        0x42911b00, 0x2000d105, 0xffbef7ff, 0x0500f8c5, 0x4613bd38, 0x4b04f853,
        0x461a5014, 0xbf00e7f1, 0x5004E000, 0x00000000
    ],
    'pc_init': 0x20000015,
    'pc_unInit': 0x20000019,
    'pc_program_page': 0x20000065,
    'pc_erase_sector': 0x20000019,
    'pc_eraseAll': 0x2000001d,
    'static_base': 0x200000a4,
    'begin_stack': 0x20000300,
    'page_size': 4,
    'analyzer_supported': False,
    'analyzer_address': 0,
    'page_buffers': [0x20001000, 0x20001004],
    'min_program_length': 4,
    'ro_start': 4,
    'ro_size': 0xa0,
    'rw_start': 0xa4,
    'rw_size': 0,
    'zi_start': 0xa4,
    'zi_size': 0,
    'flash_start': 0,
    'flash_size': 0x1FD000,
    'sector_sizes': ((0, 0x1FD000),)
}

target_cls = pyocd.target.builtin.BUILTIN_TARGETS.get('nrf54lm20a')
if target_cls:
    for region in target_cls.MEMORY_MAP.regions:
        if getattr(region, 'is_boot_memory', False):
            region.algo = FIXED_ALGO
            break
