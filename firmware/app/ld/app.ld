/*
 * Application linker script for STM32C562RE, slot A.
 *
 * The application is linked at 0x08008000 (128 KB). This is the address that
 * the bootloader copies the downloaded image to and the address the OTA image
 * must be linked for. Slot B is never linked directly; it only stores a copy
 * of an image that is already linked for slot A.
 *
 * The application writes slot B (bank 2) while executing from slot A (bank 1);
 * STM32C5 dual-bank read-while-write makes that a legal, non-stalling
 * operation, so no .ramfunc section is needed here.
 */

ENTRY(Reset_Handler)

HEAP_SIZE  = 0x800;
STACK_SIZE = 0x1000;

MEMORY
{
  ROM (rx)  : ORIGIN = 0x08008000, LENGTH = 0x20000
  RAM (xrw) : ORIGIN = 0x20000000, LENGTH = 0x20000
}

SECTIONS
{
  .vectors :
  {
    . = ALIGN(8);
    KEEP(*(.vectors));
    . = ALIGN(8);
  } > ROM

  .text :
  {
    . = ALIGN(8);
    *(.text);
    *(.text*);
    *(.glue_7);
    *(.glue_7t);
    *(.eh_frame);
    KEEP(*(.init));
    KEEP(*(.fini));
    . = ALIGN(8);
    _etext = .;
  } > ROM

  .rodata :
  {
    . = ALIGN(8);
    *(.rodata);
    *(.rodata*);
    . = ALIGN(8);
  } > ROM

  .ARM.extab (READONLY) :
  {
    . = ALIGN(8);
    *(.ARM.extab* .gnu.linkonce.armextab.*)
    . = ALIGN(8);
  } > ROM

  .ARM (READONLY) :
  {
    . = ALIGN(8);
    __exidx_start = .;
    *(.ARM.exidx*);
    __exidx_end = .;
    . = ALIGN(8);
  } > ROM

  .preinit_array (READONLY) :
  {
    . = ALIGN(8);
    PROVIDE_HIDDEN(__preinit_array_start = .);
    KEEP(*(.preinit_array*));
    PROVIDE_HIDDEN(__preinit_array_end = .);
    . = ALIGN(8);
  } > ROM

  .init_array (READONLY) :
  {
    . = ALIGN(8);
    PROVIDE_HIDDEN(__init_array_start = .);
    KEEP(*(SORT(.init_array.*)));
    KEEP(*(.init_array*));
    PROVIDE_HIDDEN(__init_array_end = .);
    . = ALIGN(8);
  } > ROM

  .fini_array (READONLY) :
  {
    . = ALIGN(8);
    PROVIDE_HIDDEN(__fini_array_start = .);
    KEEP(*(SORT(.fini_array.*)));
    KEEP(*(.fini_array*));
    PROVIDE_HIDDEN(__fini_array_end = .);
    . = ALIGN(8);
  } > ROM

  .copy.table (READONLY) :
  {
    . = ALIGN(8);
    __copy_table_start__ = .;
    LONG(LOADADDR(.data));
    LONG(ADDR(.data));
    LONG(SIZEOF(.data) / 4);
    __copy_table_end__ = .;
  } > ROM

  .zero.table (READONLY) :
  {
    . = ALIGN(8);
    __zero_table_start__ = .;
    LONG(ADDR(.bss));
    LONG(SIZEOF(.bss) / 4);
    __zero_table_end__ = .;
  } > ROM

  .data :
  {
    . = ALIGN(8);
    _sidata = LOADADDR(.data);
    __data_start__ = .;
    _sdata = .;
    *(.data);
    *(.data*);
    . = ALIGN(8);
    _edata = .;
  } > RAM AT> ROM

  .bss :
  {
    . = ALIGN(8);
    _sbss = .;
    __bss_start__ = _sbss;
    *(.bss);
    *(.bss*);
    *(COMMON);
    . = ALIGN(8);
    _ebss = .;
    __bss_end__ = _ebss;
  } > RAM

  .heap (NOLOAD) :
  {
    . = ALIGN(8);
    __end__ = .;
    PROVIDE(end = .);
    _heap_start = .;
    . += HEAP_SIZE;
    . = ALIGN(8);
    _heap_end = .;
    __HeapLimit = .;
  } > RAM

  .stack (NOLOAD) :
  {
    . = ALIGN(8);
    __StackLimit = .;
    . += STACK_SIZE;
    . = ALIGN(8);
    __StackTop = .;
    _estack = .;
    __stack = .;
  } > RAM

  .ARM.attributes 0 :
  {
    *(.ARM.attributes)
  }
}
