/* Host-test stub for BearSSL's system PRNG seeder.
 *
 * BearSSL's ssl_engine.c (br_ssl_engine_init_rand) references
 * br_prng_seeder_system, which lives in src/rand/sysrng.c — deliberately
 * filtered out of BEARSSL_SRCS in the Makefile (the kernel provides its
 * own; CapyOS seeds the DRBG explicitly rather than via an OS seeder).
 *
 * Host tests that link BearSSL (e.g. tests/security/test_tls_client_engine.c)
 * need the same symbol. Mirroring the kernel stub in
 * src/arch/x86_64/stubs.c, this returns 0 (no system seeder): tests inject
 * entropy via br_ssl_engine_inject_entropy, and ring-3 seeds the DRBG from
 * SYS_GETRANDOM (Etapa 5 / Slice 5.1). */
#include "bearssl.h"

br_prng_seeder br_prng_seeder_system(const char **name) {
  if (name) {
    *name = "none";
  }
  return 0;
}
