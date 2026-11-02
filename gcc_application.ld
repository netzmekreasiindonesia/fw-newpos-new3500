OUTPUT_ARCH(arm)
ENTRY(_startapp)
MEMORY
{
    FLASH (rx) : ORIGIN = 0x70010000, LENGTH = 0x00230000
    SRAM (rwx) : ORIGIN = 0x807c0000, LENGTH = 0x00040000
}
SECTIONS
{
    .text :
    {
        __text_start__ = .;
        KEEP(*(.vectors))
        KEEP(*(.FILEINFO))
        CREATE_OBJECT_SYMBOLS
        *(.text .text.* .gnu.linkonce.t.*)
        KEEP(*(.text.*personality*))
        *(.glue_7t) *(.glue_7) *(.vfp11_veneer)
        KEEP(*(.fini))
        *(.gcc_except_table)
    } > FLASH
    . = ALIGN(4);
    .init : { KEEP(*(.init)) } > FLASH
    .fini : { KEEP(*(.fini)) } > FLASH
    .preinit_array :
    {
        PROVIDE_HIDDEN(__preinit_array_start = .);
        KEEP(*(.preinit_array))
        PROVIDE_HIDDEN(__preinit_array_end = .);
    } > FLASH
    .init_array :
    {
        PROVIDE_HIDDEN(__init_array_start = .);
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array))
        PROVIDE_HIDDEN(__init_array_end = .);
    } > FLASH
    .fini_array :
    {
        PROVIDE_HIDDEN(__fini_array_start = .);
        KEEP(*(.fini_array))
        KEEP(*(SORT(.fini_array.*)))
        PROVIDE_HIDDEN(__fini_array_end = .);
    } > FLASH
    .app_early_init_array :
    {
        PROVIDE(__start_app_early_initcall = .);
        KEEP(*(.app.initcall1.init))
        KEEP(*(.app.initcall2.init))
        PROVIDE(__stop_app_early_initcall = .);
    } > FLASH
    .app_late_init_array :
    {
        PROVIDE(__start_app_late_initcall = .);
        KEEP(*(.app.initcall3.init))
        KEEP(*(.app.initcall4.init))
        PROVIDE(__stop_app_late_initcall = .);
    } > FLASH
    .app_logger_init_array :
    {
        PROVIDE(__start_app_logger_init = .);
        KEEP(*(.app.logger.init))
        PROVIDE(__stop_app_logger_init = .);
    } > FLASH
    .app_communication_ops_array :
    {
        PROVIDE(__start_app_comm_ops = .);
        KEEP(*(.app.comm.ops))
        PROVIDE(__stop_app_comm_ops = .);
    } > FLASH
    .ctors :
    {
        KEEP(*crtbegin.o(.ctors))
        KEEP(*crtbegin?.o(.ctors))
        KEEP(*(EXCLUDE_FILE(*crtend.o *crtend?.o) .ctors))
        KEEP(*(SORT(.ctors.*)))
        KEEP(*(.ctors))
    } > FLASH
    .dtors :
    {
        KEEP(*crtbegin.o(.dtors))
        KEEP(*crtbegin?.o(.dtors))
        KEEP(*(EXCLUDE_FILE(*crtend.o *crtend?.o) .dtors))
        KEEP(*(SORT(.dtors.*)))
        KEEP(*(.dtors))
    } > FLASH
    .rodata : { . = ALIGN(4); *(.rodata .rodata.*) *(.gnu.linkonce.r.*) } > FLASH
    export_symbol : { . = ALIGN(4); PROVIDE(__start_export_symbol = .); KEEP(*(export_symbol)); PROVIDE(__stop_export_symbol = .); } > FLASH
    initcall1init : { . = ALIGN(4); PROVIDE(__start_initcall1init = .); KEEP(*(initcall1init)); PROVIDE(__stop_initcall1init = .); } > FLASH
    initcall2init : { . = ALIGN(4); PROVIDE(__start_initcall2init = .); KEEP(*(initcall2init)); PROVIDE(__stop_initcall2init = .); } > FLASH
    initcall3init : { . = ALIGN(4); PROVIDE(__start_initcall3init = .); KEEP(*(initcall3init)); PROVIDE(__stop_initcall3init = .); } > FLASH
    initcall4init : { . = ALIGN(4); PROVIDE(__start_initcall4init = .); KEEP(*(initcall4init)); PROVIDE(__stop_initcall4init = .); } > FLASH
    initcall5init : { . = ALIGN(4); PROVIDE(__start_initcall5init = .); KEEP(*(initcall5init)); PROVIDE(__stop_initcall5init = .); } > FLASH
    initcall6init : { . = ALIGN(4); PROVIDE(__start_initcall6init = .); KEEP(*(initcall6init)); PROVIDE(__stop_initcall6init = .); } > FLASH
    initcall7init : { . = ALIGN(4); PROVIDE(__start_initcall7init = .); KEEP(*(initcall7init)); PROVIDE(__stop_initcall7init = .); } > FLASH
    window_init1 : { . = ALIGN(4); PROVIDE(__start_window_init1 = .); KEEP(*(window_init1)); PROVIDE(__stop_window_init1 = .); } > FLASH
    window_init2 : { . = ALIGN(4); PROVIDE(__start_window_init2 = .); KEEP(*(window_init2)); PROVIDE(__stop_window_init2 = .); } > FLASH
    window_init3 : { . = ALIGN(4); PROVIDE(__start_window_init3 = .); KEEP(*(window_init3)); PROVIDE(__stop_window_init3 = .); } > FLASH
    export_api : { . = ALIGN(4); PROVIDE(__start_export_api = .); KEEP(*(export_api)); PROVIDE(__stop_export_api = .); } > FLASH
    charset_rodata : { . = ALIGN(4); PROVIDE(__start_charset_rodata = .); KEEP(*(charset_rodata)); PROVIDE(__stop_charset_rodata = .); } > FLASH
    development_test_module : { . = ALIGN(4); PROVIDE(__start_development_test_module = .); KEEP(*(development_test_module)); PROVIDE(__stop_development_test_module = .); } > FLASH
    module_prod : { . = ALIGN(4); PROVIDE(__start_module_prod = .); KEEP(*(module_prod)); PROVIDE(__stop_module_prod = .); } > FLASH
    module_priv : { . = ALIGN(4); PROVIDE(__start_module_priv = .); KEEP(*(module_priv)); PROVIDE(__stop_module_priv = .); } > FLASH
    screen_mod : { . = ALIGN(4); PROVIDE(__start_screen_mod = .); KEEP(*(screen_mod)); PROVIDE(__stop_screen_mod = .); } > FLASH
    screen_mod_sys : { . = ALIGN(4); PROVIDE(__start_screen_mod_sys = .); KEEP(*(screen_mod_sys)); PROVIDE(__stop_screen_mod_sys = .); } > FLASH
    global_init : { . = ALIGN(4); PROVIDE(__start_global_init = .); KEEP(*(global_init)); PROVIDE(__stop_global_init = .); } > FLASH
    .ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } > FLASH
    __exidx_start = .;
    .ARM.exidx : { *(.ARM.exidx* .gnu.linkonce.armexidx.*) } > FLASH
    __exidx_end = .;
    PROVIDE(__etext = .);
    PROVIDE(_etext = .);
    PROVIDE(etext = .);
    __text_end__ = .;
    . = ALIGN(4);
    .data : AT(etext)
    {
        __data_start__ = .;
        *(.data .data.* .gnu.linkonce.d.*)
        SORT(CONSTRUCTORS)
    } > SRAM
    .data1 : { *(.data1) }
    _edata = .; PROVIDE(edata = .);
    __data_end__ = .;
    __bss_start = .;
    __bss_start__ = .;
    .bss (NOLOAD) :
    {
        *(.dynbss) *(.bss .bss.* .gnu.linkonce.b.*) *(COMMON)
        . = ALIGN(. != 0 ? 32 / 8 : 1);
    } > SRAM
    . = ALIGN(16);
    _bss_end__ = .;
    __bss_end__ = .;
    __end__ = .;
    _end = .;
    PROVIDE(end = .);
    ASSERT(SIZEOF(.text) + SIZEOF(.rodata) + SIZEOF(.data) <= LENGTH(FLASH), "Text exceeds flash")
    ASSERT(SIZEOF(.data) + SIZEOF(.bss) <= LENGTH(SRAM), "Data exceeds RAM")
}