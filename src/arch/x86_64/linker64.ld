OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

SECTIONS
{
    /* Carregar em 4M (0x400000): VMware UEFI reserva paginas internas
       em 16M+ (gera firmware exception na escrita) e tambem em 256M+.
       4M e seguro: acima do BIOS-compat (~1MB) e fora das janelas
       internas do firmware VMware. Verificado em sessao anterior. */
    . = 4M;

    .text : ALIGN(4K) {
        *(.text)
        *(.text.*)
    }

    .rodata : ALIGN(4K) {
        *(.rodata)
        *(.rodata.*)
    }

    .data : ALIGN(4K) {
        *(.data)
        *(.data.*)
    }

    .got : ALIGN(8) {
        *(.got)
        *(.got.*)
        *(.got.plt)
        *(.got.plt.*)
    }

    .bss (NOLOAD) : ALIGN(4K) {
        PROVIDE(__bss_start = .);
        *(.bss)
        *(.bss.*)
        *(COMMON)
        PROVIDE(__bss_end = .);
    }

    .bootstrap_stack (NOLOAD) : ALIGN(4K) {
        KEEP(*(.bootstrap_stack))
        KEEP(*(.bootstrap_stack.*))
    }
}
