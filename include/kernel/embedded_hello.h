#ifndef KERNEL_EMBEDDED_HELLO_H
#define KERNEL_EMBEDDED_HELLO_H

#include <stddef.h>
#include <stdint.h>

/* The first user-space binary (`userland/bin/hello/hello.elf`) is
 * embedded directly into the kernel image as a `.rodata` blob by an
 * `objcopy --rename-section .data=.rodata` step (see Makefile rule
 * for `$(HELLO_BLOB_OBJ)`). objcopy emits three magic symbols when
 * it lifts a binary into ELF:
 *
 *   _binary_hello_elf_start  -> address of first byte
 *   _binary_hello_elf_end    -> address one past the last byte
 *   _binary_hello_elf_size   -> the SIZE encoded as the address of
 *                               this symbol (NOT a value held there).
 *                               That is, `(size_t)&_binary_hello_elf_size`
 *                               is the byte count.
 *
 * The kernel never reads these externs directly. Use the helpers
 * below: they hide the address-vs-value quirk of `_binary_*_size`
 * and give the C side a clean (data, size) pair ready to feed into
 * `elf_load_into_process`. */

extern const uint8_t _binary_hello_elf_start[];
extern const uint8_t _binary_hello_elf_end[];
extern const uint8_t _binary_hello_elf_size[];

/* Returns a pointer to the embedded hello.elf blob (kernel virtual
 * address). The blob lives in kernel `.rodata`, so the caller must
 * not write to it. */
const void *embedded_hello_data(void);

/* Returns the blob length in bytes. */
size_t embedded_hello_size(void);

#endif /* KERNEL_EMBEDDED_HELLO_H */
