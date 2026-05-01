OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

SECTIONS
{
    /* Carregar em 64M: a imagem em memoria inclui uma .bss grande
       (heap estatico + caches) e precisa de uma janela contigua de
       dezenas de MiB. Em QEMU/OVMF, a janela baixa iniciando em 4M
       pode cruzar regioes ocupadas pelo firmware no boot pelo disco. */
    . = 64M;
    PROVIDE(__kernel_image_start = .);

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
    . = ALIGN(4K);
    PROVIDE(__kernel_image_end = .);
}
