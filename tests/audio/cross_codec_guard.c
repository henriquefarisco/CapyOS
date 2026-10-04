#include <stdint.h>

/* Hosted replay of freestanding objects uses the cross compiler's global
 * stack guard ABI, not glibc's TLS guard. This is test-only, never linked into
 * the kernel; guard failures still use glibc's terminating handler. */
uintptr_t __stack_chk_guard = UINT64_C(0x781bc734442ac600);
