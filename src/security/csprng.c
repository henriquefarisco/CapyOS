#include "security/csprng.h"
#include "security/crypt.h"
#include <stddef.h>
#include <stdint.h>

// Contexto global de entropia
static struct sha256_ctx entropy_pool;
static uint64_t reseed_counter = 0;
// Flag para saber se foi inicializado
static int initialized = 0;

/*
 * Precisamos de primitivas de trava para evitar corrida entre ISR
 * e cÃ³digo de usuÃ¡rio. Como estamos em uniprocessador, desabilitar
 * interrupÃ§Ãµes basta.
 */
#ifdef UNIT_TEST
static inline unsigned long irq_save(void) { return 0; }
static inline void irq_restore(unsigned long flags) { (void)flags; }
#else
static inline unsigned long irq_save(void) {
  unsigned long flags;
  // Save EFLAGS and disable interrupts (CLI)
  // We strictly need to pushf, pop into var, then cli.
  // In GCC inline asm:
  __asm__ volatile("pushf; pop %0; cli" : "=rm"(flags) : : "memory");
  return flags;
}

static inline void irq_restore(unsigned long flags) {
  // Restore EFLAGS (which contains IF state)
  __asm__ volatile("push %0; popf" : : "rm"(flags) : "memory", "cc");
}
#endif

static int has_rdrand(void) {
#ifdef UNIT_TEST
  return 0;
#else
  uint32_t ecx = 0;
  __asm__ volatile("cpuid" : "=c"(ecx) : "a"(1) : "ebx", "edx");
  return (ecx >> 30) & 1;
#endif
}

static int rdrand64(uint64_t *out) {
#ifdef UNIT_TEST
  (void)out;
  return 0;
#else
  uint64_t val;
  unsigned char ok;
  __asm__ volatile("rdrand %0; setc %1" : "=r"(val), "=qm"(ok));
  if (ok) { *out = val; return 1; }
  return 0;
#endif
}

void csprng_init(void) {
  if (initialized)
    return;
  sha256_init(&entropy_pool);
  uint8_t salt[] = "CAPYOS_Initial_Salt_v1";
  sha256_update(&entropy_pool, salt, sizeof(salt));

  /* Mix RDRAND hardware entropy when available */
  if (has_rdrand()) {
    for (int i = 0; i < 4; i++) {
      uint64_t hw = 0;
      if (rdrand64(&hw)) {
        sha256_update(&entropy_pool, (uint8_t *)&hw, sizeof(hw));
      }
    }
  }

  /* Mix TSC as additional entropy source */
#ifndef UNIT_TEST
  {
    uint64_t tsc;
    __asm__ volatile("rdtsc" : "=A"(tsc));
    sha256_update(&entropy_pool, (uint8_t *)&tsc, sizeof(tsc));
  }
#endif

  reseed_counter = 0;
  initialized = 1;
}

void csprng_feed_entropy(uint32_t data) {
  unsigned long flags;
  if (!initialized)
    csprng_init();

  // Protect against race: ensure atomic update even if called from thread
  // context and interrupted, or called from nested interrupts (if supported,
  // though rare here).
  flags = irq_save();

  sha256_update(&entropy_pool, (uint8_t *)&data, sizeof(data));
  reseed_counter++;

  irq_restore(flags);
}

void csprng_get_bytes(void *buf, size_t len) {
  if (!initialized)
    csprng_init();
  uint8_t *p = (uint8_t *)buf;

  unsigned long flags = irq_save(); // RegiÃ£o crÃ­tica: acesso ao pool global

  while (len > 0) {
    // Snapshot do pool atual
    uint8_t digest[32];
    struct sha256_ctx temp_ctx = entropy_pool; // Copia estado atual

    // Finaliza cÃ³pia para gerar bloco
    sha256_update(&temp_ctx, (uint8_t *)&reseed_counter,
                  sizeof(reseed_counter));
    sha256_final(&temp_ctx, digest);

    // Atualiza pool com o prÃ³prio output para "forward secrecy" (rekey)
    sha256_update(&entropy_pool, digest, 32);
    reseed_counter++;

    // Copia para saÃ­da
    size_t to_copy = (len < 32) ? len : 32;
    for (size_t i = 0; i < to_copy; ++i) {
      *p++ = digest[i];
    }
    len -= to_copy;

    // Limpa segredos da stack
    for (volatile int i = 0; i < 32; ++i)
      digest[i] = 0;
  }

  irq_restore(flags);
}

uint32_t csprng_next_u32(void) {
  uint32_t val = 0;
  csprng_get_bytes(&val, sizeof(val));
  return val;
}
