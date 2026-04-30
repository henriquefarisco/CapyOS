/*
 * Thin wrapper around the linker-provided `_binary_hello_elf_*`
 * symbols. The .o that defines these symbols is produced by an
 * objcopy step in the Makefile and linked into the kernel; this
 * source compiles unconditionally and only references the externs
 * inside the helper bodies, so the kernel build's link step is the
 * single point that may fail when the user binary is missing.
 *
 * The unit-test build excludes this TU on purpose: host tests do
 * not (and should not) link against a real kernel objcopy artifact.
 * Tests that need a faux blob ship their own data via stubs.
 */
#include "kernel/embedded_hello.h"

const void *embedded_hello_data(void) {
  const uint8_t *start;
  __asm__ volatile("lea _binary_hello_elf_start(%%rip), %0"
                   : "=r"(start));
  return (const void *)start;
}

size_t embedded_hello_size(void) {
  const uint8_t *start;
  const uint8_t *end;
  __asm__ volatile("lea _binary_hello_elf_start(%%rip), %0"
                   : "=r"(start));
  __asm__ volatile("lea _binary_hello_elf_end(%%rip), %0"
                   : "=r"(end));
  return (size_t)(end - start);
}
