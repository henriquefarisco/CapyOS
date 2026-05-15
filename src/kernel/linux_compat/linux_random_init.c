#include "kernel/linux_compat/linux_random.h"

/* Boot wiring for `linux_random` against the real kernel CSPRNG.
 * Excluded from host unit tests (which inject their own deterministic
 * generator).
 *
 * The CapyOS CSPRNG is `src/security/csprng.c` -- a SHA-256 pool fed
 * by interrupt-time entropy via `csprng_feed_entropy`. It is
 * initialised lazily, but `linux_random_init_boot()` calls
 * `csprng_init()` explicitly to make the seed deterministic relative
 * to the boot timeline.
 */

#if !defined(UNIT_TEST)

#include "security/csprng.h"

void linux_random_init_boot(void) {
  csprng_init();
  linux_random_install_source(csprng_get_bytes);
}

#endif /* !UNIT_TEST */
