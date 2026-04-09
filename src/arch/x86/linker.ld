/* Linker script para kernel 32-bit ELF com Multiboot */
ENTRY(_start)
OUTPUT_FORMAT(elf32-i386)
OUTPUT_ARCH(i386)

/* Dois segmentos PT_LOAD:
   - text: R+X
   - data: R+W */
PHDRS
{
  text PT_LOAD FLAGS(5); /* R=4, X=1 */
  data PT_LOAD FLAGS(6); /* R=4, W=2 */
}

SECTIONS
{
  /* Endereço lógico de link — 1MiB é comum */
  . = 1M;

  /* Header Multiboot precisa vir cedo no binário */
  .multiboot : ALIGN(16)
  {
    KEEP(*(.multiboot))
  } :text

  .text : ALIGN(16)
  {
    *(.text .text.*)
  } :text

  .rodata : ALIGN(16)
  {
    *(.rodata .rodata.*)
    *(.gnu.linkonce.r*)
  } :text

  .data : ALIGN(16)
  {
    *(.data .data.*)
    *(.gnu.linkonce.d*)
  } :data

  .bss (NOLOAD) : ALIGN(16)
  {
    __bss_start = .;
    *(COMMON)
    *(.bss .bss.*)
    __bss_end = .;
  } :data

  /DISCARD/ :
  {
    *(.eh_frame) *(.eh_frame.*)
    *(.comment) *(.note*) *(.gnu_debuglink) *(.gnu.version*)
  }
}
