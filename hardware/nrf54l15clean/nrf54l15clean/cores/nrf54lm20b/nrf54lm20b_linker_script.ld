/*
 * Linker Script for nRF54LM20B (shared nRF54L15 core)
 *
 * Memory Layout (no FLPR co-processor, all RAM for CPUAPP):
 * - RRAM:  2036 KB total (0x00000000 - 0x001FD000)
 *   - 4 KB reserved at the top for bond/preferences/EEPROM persistence
 *   - 2032 KB usable for code/rodata/data loads
 * - RAM:   512 KB total (0x20000000 - 0x20080000)
 *   - All RAM available for CPUAPP runtime sections
 *   - No FLPR/NPU reservation (Arduino doesn't use these coprocessors)
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
    RAM (rwx)       : ORIGIN = 0x20000000, LENGTH = 0x40000
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

    .bond_storage (NOLOAD) :
    {
        . = ALIGN(4);
        _sbond_storage = .;
        KEEP(*(.bond_storage))
        KEEP(*(.bond_storage*))
        . = ALIGN(4);
        _ebond_storage = .;
    } >FLASH_BOND

    .prefs_storage (NOLOAD) :
    {
        . = ALIGN(4);
        _sprefs_storage = .;
        KEEP(*(.prefs_storage))
        KEEP(*(.prefs_storage*))
        . = ALIGN(4);
        _eprefs_storage = .;
    } >FLASH_BOND

    .eeprom_storage (NOLOAD) :
    {
        . = ALIGN(4);
        _seeprom_storage = .;
        KEEP(*(.eeprom_storage))
        KEEP(*(.eeprom_storage*))
        . = ALIGN(4);
        _eeeprom_storage = .;
    } >FLASH_BOND

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

    __stack_start__ = ORIGIN(RAM) + LENGTH(RAM);
    __stack_end__ = __stack_start__ - __stack_size__;

    PROVIDE(__heap_end__ = ORIGIN(RAM) + LENGTH(RAM));

    . = ALIGN(4);
}
