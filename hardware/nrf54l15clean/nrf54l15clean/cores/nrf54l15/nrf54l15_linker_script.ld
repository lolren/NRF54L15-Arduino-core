/*
 * Linker Script for nRF54L15
 *
 * Memory Layout:
 * - RRAM:  1524 KB total (0x00000000 - 0x0017D000)
 *   - 8 KB reserved at the top for multibond and legacy persistence
 *   - 1516 KB usable for code/rodata/data loads
 * - RAM:   256 KB total (0x20000000 - 0x20040000)
 *   - CPUAPP runtime RAM is limited to the low 152 KB (0x20000000 - 0x20026000)
 *   - 4 KB is reserved in application SRAM for CPUAPP<->VPR shared transport:
 *     - VPR->host RX window  (0x20018000 - 0x20018800)
 *     - host->VPR TX window  (0x20020000 - 0x20020800)
 *   - The heap is capped below the first shared IPC window so the transport
 *     buffers are not overwritten by host-side allocations
 *   - 104 KB top-of-RAM window is reserved for VPR/FLPR execution and saved context
 *   - 28416 B reserved for a VPR helper image (0x20039000 - 0x2003FF00)
 *   - top 256 B reserved for VPR saved context (0x2003FF00 - 0x20040000)
 *   - 152 KB usable for CPUAPP runtime sections when VPR support is enabled
 *
 * Licensed under the Apache License 2.0
 */

/* Entry Point */
ENTRY(Reset_Handler)

/* Memory Regions */
MEMORY
{
    FLASH (rx)      : ORIGIN = 0x00000000, LENGTH = 0x17B000
    FLASH_BOND_DB (r) : ORIGIN = 0x0017B000, LENGTH = 0x1000
    FLASH_BOND (r)  : ORIGIN = 0x0017C000, LENGTH = 0x1000
    RAM (rwx)       : ORIGIN = 0x20000000, LENGTH = 0x26000
}

/* Stack size. Heap is not given a fixed size; it fills all RAM between
 * the end of static sections (.bss, .noinit) and the bottom of the stack. */
__stack_size__ = 4K;

/* Section Definitions */
SECTIONS
{
    /*
     * The startup code goes into FLASH
     */
    .isr_vector :
    {
        . = ALIGN(4);
        KEEP(*(.isr_vector))     /* Startup code */
        . = ALIGN(4);
    } >FLASH

    /*
     * The program code and other data goes into FLASH
     */
    .text :
    {
        . = ALIGN(4);
        *(.text)                 /* .text sections (code) */
        *(.text*)                /* .text* sections (code) */
        *(.glue_7)               /* glue arm to thumb code */
        *(.glue_7t)              /* glue thumb to arm code */
        *(.eh_frame)             /* exception handling */

        KEEP (*(.init))
        KEEP (*(.fini))

        . = ALIGN(4);
        _etext = .;              /* End of code section */
    } >FLASH

    /*
     * C/C++ runtime constructor/destructor tables.
     * __libc_init_array() consumes these symbols at startup.
     */
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

    /*
     * Read-only data (constants, strings, etc.)
     */
    .rodata :
    {
        . = ALIGN(4);
        *(.rodata)               /* .rodata sections (constants, strings, etc.) */
        *(.rodata*)              /* .rodata* sections */
        . = ALIGN(4);
    } >FLASH

    /* Eight logical BLE bonds, each stored as two 256-byte replicas. */
    .bond_db_storage ORIGIN(FLASH_BOND_DB) (NOLOAD) :
    {
        . = ALIGN(16);
        _sbond_db_storage = .;
        KEEP(*(.bond_db_storage))
        . = ALIGN(16);
        _ebond_db_storage = .;
    } >FLASH_BOND_DB
    ASSERT(SIZEOF(.bond_db_storage) <= 0x1000,
           "BLE multibond database exceeds its dedicated 4 KB page")

    /*
     * Reserved non-volatile storage area for bond persistence.
     * This region is excluded from normal code/data placement.
     */
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

    /*
     * Reserved non-volatile storage area for user Preferences key/value data.
     * Shares the dedicated FLASH_BOND page with bond storage.
     */
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

    /*
     * Reserved non-volatile storage area for EEPROM emulation.
     * Shares the dedicated FLASH_BOND page with bond/prefs storage.
     */
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

    /* Versioned CSRK/counter extension in the legacy page's 56-byte slack. */
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

    /*
     * ARM sections
     */
    .ARM.extab :
    {
        *(.ARM.extab* .gnu.linkonce.armextab.*)
    } >FLASH

    .ARM :
    {
        __exidx_start = .;
        *(.ARM.exidx*)
        __exidx_end = .;
    } >FLASH

    /*
     * Initialized data sections into RAM, load from FLASH
     */
    .data :
    {
        . = ALIGN(4);
        _sdata = .;              /* Start of data section in RAM */
        *(.data)                 /* .data sections */
        *(.data*)                /* .data* sections */

        . = ALIGN(4);
        _edata = .;              /* End of data section in RAM */
    } >RAM AT>FLASH

    _sidata = LOADADDR(.data);   /* Start of data section in FLASH */

    /*
     * Uninitialized data section into RAM
     */
    .bss :
    {
        . = ALIGN(4);
        _sbss = .;               /* Start of bss section */
        __bss_start__ = _sbss;
        *(.bss)
        *(.bss*)
        *(COMMON)

        . = ALIGN(4);
        _ebss = .;               /* End of bss section */
        __bss_end__ = _ebss;
    } >RAM

    /*
     * Non-initialized RAM section retained across reset paths where RAM power
     * is maintained. Startup code does not touch this section.
     */
    .noinit (NOLOAD) :
    {
        . = ALIGN(4);
        _snoinit = .;
        *(.noinit)
        *(.noinit*)
        . = ALIGN(4);
        _enoinit = .;
    } >RAM

    /*
     * Heap: grows upward from end of static sections, but is capped below the
     * first CPUAPP<->VPR shared-memory window.
     * Stack: the startup code sets SP to __stack_start__ (top of RAM); the
     * linker symbol __stack_end__ marks the lowest safe stack address.
     */
    ._user_heap_stack :
    {
        . = ALIGN(8);
        PROVIDE ( __heap_start__ = . );
        PROVIDE ( end = . );
        PROVIDE ( _end = . );
        . = ALIGN(8);
    } >RAM
    /*
     * Symbols for startup code
     */
    __stack_start__ = ORIGIN(RAM) + LENGTH(RAM);
    __stack_end__ = __stack_start__ - __stack_size__;

    /*
     * Fixed VPR helper memory windows reserved outside the normal M33 runtime
     * heap range. The transport uses the same CPUAPP SRAM windows that Nordic
     * reserves for CPUAPP<->FLPR IPC in Zephyr.
     */
    PROVIDE(__vpr_image_start__ = 0x20039000);
    PROVIDE(__vpr_image_end__ = 0x2003FF00);
    PROVIDE(__vpr_shared_start__ = 0x20018000);
    PROVIDE(__vpr_shared_end__ = 0x20020800);
    PROVIDE(__vpr_shared_host_start__ = 0x20020000);
    PROVIDE(__vpr_shared_host_end__ = 0x20020800);
    PROVIDE(__vpr_shared_vpr_start__ = 0x20018000);
    PROVIDE(__vpr_shared_vpr_end__ = 0x20018800);
    PROVIDE(__heap_end__ = 0x20018000);
    PROVIDE(__vpr_context_save_start__ = 0x2003FF00);
    PROVIDE(__vpr_context_save_end__ = 0x20040000);

    ASSERT(_enoinit <= 0x20018000, "CPUAPP RAM sections overlap VPR IPC window")


    . = ALIGN(4);
}
