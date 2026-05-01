#ifndef KERNEL_EMBEDDED_PROGS_H
#define KERNEL_EMBEDDED_PROGS_H

#include <stddef.h>
#include <stdint.h>

/* M5 phase B.2: in-kernel registry of binaries embedded into the
 * kernel image at link time.
 *
 * Until CapyOS has a real on-disk filesystem path that user code
 * can `exec()` against, we ship a tiny set of canonical user
 * binaries embedded as `.rodata` blobs (see Makefile rules for
 * `$(HELLO_BLOB_OBJ)` and `$(EXECTARGET_BLOB_OBJ)`). The registry
 * exposes them via stable string paths so SYS_EXEC can route a
 * userspace `path` argument to the matching `(data, size)` pair.
 *
 * Currently registered:
 *
 *   "/bin/hello"        -> userland/bin/hello/hello.elf
 *   "/bin/exectarget"   -> userland/bin/exectarget/exectarget.elf
 *   "/bin/capysh"       -> userland/bin/capysh/capysh.elf
 *   "/bin/capybrowser"  -> userland/bin/capybrowser/capybrowser.elf  (F3.3b)
 *
 * Adding a binary:
 *
 *   1. Create userland/bin/<name>/main.c.
 *   2. Wire <name>_ELF + <name>_BLOB_OBJ rules in the Makefile,
 *      mirroring the hello pattern.
 *   3. Append the BLOB_OBJ to CAPYOS64_OBJS.
 *   4. Register an entry below by extending embedded_progs.c.
 *   5. Add or extend a host test in tests/test_embedded_progs.c.
 *
 * Lookup is O(N) string compare; we have N=2 today and the call
 * site is on the SYS_EXEC slow path. */

/* Looks `path` up against the registered table.
 *
 * On hit: writes the blob's start pointer and byte length into
 * *out_data / *out_size and returns 0.
 * On miss or NULL inputs: returns -1, leaves outputs unchanged.
 *
 * The returned pointer aliases kernel `.rodata`; the caller MUST
 * NOT write through it. The lifetime is the kernel image's. */
int embedded_progs_lookup(const char *path,
                          const uint8_t **out_data,
                          size_t *out_size);

#endif /* KERNEL_EMBEDDED_PROGS_H */
