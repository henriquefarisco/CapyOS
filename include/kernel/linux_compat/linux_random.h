#ifndef KERNEL_LINUX_COMPAT_LINUX_RANDOM_H
#define KERNEL_LINUX_COMPAT_LINUX_RANDOM_H

#include <stdint.h>
#include <stddef.h>

/* Linux-ABI `getrandom(2)` shim (S1.8).
 *
 * The CapyOS CSPRNG (`src/security/csprng.c`) is already SHA-256 +
 * /dev/urandom-style pool with periodic entropy reseed. Linux's
 * `getrandom(buf, count, flags)` semantics map cleanly onto
 * `csprng_get_bytes`:
 *
 *   GRND_RANDOM  -> ignored: we have a single unified pool that
 *                   never blocks (the seed-quality argument has been
 *                   superseded by getrandom semantics in modern
 *                   glibc/musl anyway).
 *   GRND_NONBLOCK-> respected: we always have entropy ready, so this
 *                   is a no-op.
 *   GRND_INSECURE-> accepted: same pool, the flag was added in 5.6
 *                   for cases where speed matters more than
 *                   crypto-strength. Today our pool is already cheap.
 *
 * Return value: number of bytes written. Linux returns -1/errno; the
 * shim returns negative `-LINUX_E*` directly so dispatcher callers
 * can propagate verbatim.
 *
 * Layering: same as `linux_clock`. Pure logic accepts an injected
 * `bytes_fn` so host tests use a deterministic counter; production
 * binds to `csprng_get_bytes`.
 */

/* Linux flag bits, from `include/uapi/linux/random.h`. */
#define LINUX_GRND_NONBLOCK 0x0001
#define LINUX_GRND_RANDOM   0x0002
#define LINUX_GRND_INSECURE 0x0004

/* Mask of accepted flag bits. Anything outside -> -LINUX_EINVAL. */
#define LINUX_GRND_KNOWN_MASK \
    (LINUX_GRND_NONBLOCK | LINUX_GRND_RANDOM | LINUX_GRND_INSECURE)

/* Production CSPRNG callback. Production code installs
 * `csprng_get_bytes` at boot via `linux_random_install_source()`. */
typedef void (*linux_random_bytes_fn)(void *buf, size_t len);

void linux_random_install_source(linux_random_bytes_fn fn);
void linux_random_reset_for_tests(void);

/* Number of bytes the kernel is willing to fill in a single
 * getrandom call. Linux uses 33_554_431 (32 MiB - 1) before
 * splitting across kernel pages. We mirror that bound so userland
 * libc that retries-on-short-read sees identical behaviour. */
#define LINUX_GETRANDOM_INT_MAX 33554431u

/* Core function: returns bytes written, or `-LINUX_E*` on error.
 *   buf == NULL && len > 0           -> -LINUX_EFAULT
 *   flags has unknown bits           -> -LINUX_EINVAL
 *   no source installed              -> -LINUX_EAGAIN
 *   len == 0                         -> 0
 *   len > LINUX_GETRANDOM_INT_MAX    -> clipped to the cap then
 *                                       returned as the actual count
 */
int64_t linux_getrandom(void *buf, size_t len, uint32_t flags);

/* Register `getrandom` in the linux_syscall dispatcher table. Called
 * by `linux_syscall_init()`. */
void linux_random_register_syscalls(void);

#endif /* KERNEL_LINUX_COMPAT_LINUX_RANDOM_H */
