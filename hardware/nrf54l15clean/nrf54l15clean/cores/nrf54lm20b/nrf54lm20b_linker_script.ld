/*
 * Linker Script for nRF54LM20B (shared nRF54L15 core)
 *
 * Memory Layout (no FLPR co-processor, all RAM for CPUAPP):
 * - RRAM:  2036 KB total (0x00000000 - 0x001FD000)
 *   - 4 KB reserved at the top for bond/preferences/EEPROM persistence
 *   - 2032 KB usable for code/rodata/data loads
 * - RAM: application range 0x20000000 - 0x2007FD40 (523584 bytes)
 *   - 0x2007FD40 and above is reserved for VPR saved context and ProtectedRAM
 *   - The top 4 KB of application RAM is reserved for the CPUAPP stack
 *
 * Based on the nrf54l15_linker_script_no_vpr.ld
 *
 * Licensed under the Apache License 2.0
 */

ENTRY(Reset_Handler)

MEMORY
{
    FLASH (rx)      : ORIGIN = 0x00000000, LENGTH = 0x1FC000
    FLASH_BOND (r)  : ORIGIN = 0x001FC000, LENGTH = 0x1000
    RAM (rwx)       : ORIGIN = 0x20000000, LENGTH = 0x7FD40
}

__stack_size__ = 4K;

SECTIONS
{
    .isr_vector :
    {
        . = ALIGN(4);
        KEEP(*(.isr_vector))
        . = ALIGN(4);
    } >FLASH

    .text :
    {
        . = ALIGN(4);
        *(.text)
        *(.text*)
        *(.glue_7)
        *(.glue_7t)
        *(.eh_frame)
        KEEP (*(.init))
        KEEP (*(.fini))
        . = ALIGN(4);
        _etext = .;
    } >FLASH

    .preinit_array :
    {
        . = ALIGN(4);
        PROVIDE_HIDDEN(__preinit_array_start = .);
        KEEP(*(.preinit_array))
        KEEP(*(.preinit_array*))
        PROVIDE_HIDDEN(__preinit_array_end = .);
        . = ALIGN(4);
    } >FLASH

    .init_array :
    {
        . = ALIGN(4);
        PROVIDE_HIDDEN(__init_array_start = .);
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array))
        PROVIDE_HIDDEN(__init_array_end = .);
        . = ALIGN(4);
    } >FLASH

    .fini_array :
    {
        . = ALIGN(4);
        PROVIDE_HIDDEN(__fini_array_start = .);
        KEEP(*(SORT(.fini_array.*)))
        KEEP(*(.fini_array))
        PROVIDE_HIDDEN(__fini_array_end = .);
        . = ALIGN(4);
    } >FLASH

    .rodata :
    {
        . = ALIGN(4);
        *(.rodata)
        *(.rodata*)
        . = ALIGN(4);
    } >FLASH

    .bond_storage ORIGIN(FLASH_BOND) (NOLOAD) :
    {
        . = ALIGN(4);
        _sbond_storage = .;
        KEEP(*(.bond_base_storage))
        . = ALIGN(4);
    } >FLASH_BOND
    ASSERT(SIZEOF(.bond_storage) <= 0x50,
           "BLE base bond storage exceeds its stable 80-byte slot")

    .bond_cccd_storage ORIGIN(FLASH_BOND) + 0x50 (NOLOAD) :
    {
        . = ALIGN(4);
        KEEP(*(.bond_cccd_storage))
        . = ALIGN(4);
        _ebond_storage = .;
    } >FLASH_BOND
    ASSERT(SIZEOF(.bond_cccd_storage) <= 0x8C,
           "BLE CCCD storage exceeds its stable 140-byte slot")

    .prefs_storage ORIGIN(FLASH_BOND) + 0xDC (NOLOAD) :
    {
        . = ALIGN(4);
        _sprefs_storage = .;
        KEEP(*(.prefs_storage))
        KEEP(*(.prefs_storage*))
        . = ALIGN(4);
        _eprefs_storage = .;
    } >FLASH_BOND
    ASSERT(SIZEOF(.prefs_storage) <= 0xAE0,
           "Preferences storage exceeds its stable 2784-byte slot")

    .eeprom_storage ORIGIN(FLASH_BOND) + 0xBBC (NOLOAD) :
    {
        . = ALIGN(4);
        _seeprom_storage = .;
        KEEP(*(.eeprom_storage))
        KEEP(*(.eeprom_storage*))
        . = ALIGN(4);
        _eeeprom_storage = .;
    } >FLASH_BOND
    ASSERT(SIZEOF(.eeprom_storage) <= 0x40C,
           "EEPROM storage exceeds its stable 1036-byte slot")

    .bond_signing_storage ORIGIN(FLASH_BOND) + 0xFC8 (NOLOAD) :
    {
        . = ALIGN(4);
        _sbond_signing_storage = .;
        KEEP(*(.bond_signing_storage))
        KEEP(*(.bond_signing_storage*))
        . = ALIGN(4);
        _ebond_signing_storage = .;
    } >FLASH_BOND
    ASSERT(SIZEOF(.bond_signing_storage) <= 0x38,
           "BLE signing extension exceeds the stable 56-byte slot")

    .ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } >FLASH
    .ARM : { __exidx_start = .; *(.ARM.exidx*) __exidx_end = .; } >FLASH

    .data :
    {
        . = ALIGN(4);
        _sdata = .;
        *(.data)
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } >RAM AT>FLASH

    _sidata = LOADADDR(.data);

    .bss :
    {
        . = ALIGN(4);
        _sbss = .;
        __bss_start__ = _sbss;
        *(.bss)
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
        __bss_end__ = _ebss;
    } >RAM

    .noinit (NOLOAD) :
    {
        . = ALIGN(4);
        _snoinit = .;
        *(.noinit)
        *(.noinit*)
        . = ALIGN(4);
        _enoinit = .;
    } >RAM

    ._user_heap_stack :
    {
        . = ALIGN(8);
        PROVIDE ( __heap_start__ = . );
        PROVIDE ( end = . );
        PROVIDE ( _end = . );
        . = ALIGN(8);
    } >RAM

    __stack_top__ = ORIGIN(RAM) + LENGTH(RAM);
    __stack_bottom__ = __stack_top__ - __stack_size__;
    __stack_start__ = __stack_top__;
    __stack_end__ = __stack_bottom__;

    PROVIDE(__heap_end__ = __stack_bottom__);

    ASSERT(_end <= __heap_end__, "LM20 static data overlaps the reserved stack")
    ASSERT(__stack_top__ == 0x2007FD40,
           "LM20 RAM must end before VPR saved context")

    . = ALIGN(4);
}
