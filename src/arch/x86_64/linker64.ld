OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

SECTIONS
{
    /* Keep kernel in low memory, but avoid the ~1MiB area some firmwares/hypervisors use. */
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

    .bss : ALIGN(4K) {
        *(.bss)
        *(.bss.*)
        *(COMMON)
    }
}
