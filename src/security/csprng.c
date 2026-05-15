#include "security/csprng.h"
#include "security/crypt.h"
#include "security/sha256.h"
#include <stddef.h>
#include <stdint.h>

/*
 * Pool global de entropia, chaveado por SHA-256. O estado interno
 * (`entropy_pool`) absorve entradas via `sha256_update` e produz output
 * via snapshot + finalize, com o output sendo realimentado no pool
 * para forward secrecy. `reseed_counter` e um contador monotonico
 * incrementado a cada `feed_entropy` e a cada bloco emitido —
 * garante que dois snapshots consecutivos do mesmo estado de pool
 * produzam outputs diferentes. `bytes_since_reseed` rastreia o
 * progresso para o reseed automatico (`CSPRNG_RESEED_INTERVAL_BYTES`).
 */
static struct sha256_ctx entropy_pool;
static uint64_t reseed_counter = 0;
static size_t bytes_since_reseed = 0;
static int initialized = 0;

/*
 * Lock de exclusao mutua contra ISRs. Como o sistema e uniprocessador,
 * desabilitar interrupcoes via CLI cobre todo o cenario de
 * concorrencia. Em SMP esse lock teria que virar spinlock. UNIT_TEST
 * usa stub no-op porque o teste host-side roda single-threaded.
 */
#ifdef UNIT_TEST
static inline unsigned long irq_save(void) { return 0; }
static inline void irq_restore(unsigned long flags) { (void)flags; }
#else
static inline unsigned long irq_save(void) {
  unsigned long flags;
  __asm__ volatile("pushf; pop %0; cli" : "=rm"(flags) : : "memory");
  return flags;
}

static inline void irq_restore(unsigned long flags) {
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
  /* RDRAND can transiently fail (CF=0) under hardware contention. The
   * RFC-style retry loop (up to 10 attempts per Intel guidance) raises
   * the practical success rate to ~1 - 2^-2000. Retry-zero callers
   * upstream simply treat failure as "no RDRAND entropy this round",
   * which is safe — RDRAND is one of several sources mixed in. */
  for (int attempt = 0; attempt < 10; ++attempt) {
    __asm__ volatile("rdrand %0; setc %1" : "=r"(val), "=qm"(ok));
    if (ok) {
      *out = val;
      return 1;
    }
  }
  return 0;
#endif
}

/*
 * Leitura do TSC (Time Stamp Counter) — 64 bits, retornados em EDX:EAX.
 * A constraint `"=A"` antiga era mal-formed em x86_64: nesse modo, "A"
 * significa "the union of registers RAX and RDX" (e nao "EAX:EDX em
 * combinacao") — comportamento ambiguo. Usar duas constraints
 * separadas `=a`/`=d` torna o codigo correto em ambos os modos e
 * elimina dependencia de quirks do compilador.
 *
 * Em UNIT_TEST retornamos 0 para reprodutibilidade do test harness;
 * o teste real de qualidade do TSC e a integracao em kernel.
 */
static uint64_t read_tsc(void) {
#ifdef UNIT_TEST
  return 0u;
#else
  uint32_t lo = 0u;
  uint32_t hi = 0u;
  __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
  return ((uint64_t)hi << 32) | (uint64_t)lo;
#endif
}

/*
 * TSC jitter loop. Em ambientes onde RDRAND nao esta disponivel (VM
 * sem passthrough, CPU velha) e onde o TSC pode ser virtualizado de
 * forma deterministica, o TSC isolado nao adiciona entropia. Mas
 * sucessivas leituras do TSC entre operacoes triviais coletam
 * variacao residual de cache miss, branch predictor state e
 * scheduler preemption — fenomenos nao-deterministicos mesmo em VM.
 * Este loop mistura 16 deltas no pool. Custo: ~10 microssegundos no
 * boot path; nao executado em runtime.
 */
static void tsc_jitter_collect(void) {
#ifndef UNIT_TEST
  uint64_t prev = read_tsc();
  /* `acc` evita que o compilador otimize o loop interno como
   * dead-code. O valor final tambem entra como entropia residual. */
  volatile uint64_t acc = 0u;
  for (int round = 0; round < 16; ++round) {
    /* Trabalho fixo para amplificar a janela onde cache/predictor
     * variam entre rounds. */
    for (volatile int spin = 0; spin < 256; ++spin) {
      acc ^= ((uint64_t)spin * 0x9E3779B97F4A7C15ull) + acc;
    }
    uint64_t now = read_tsc();
    uint64_t delta = now - prev;
    sha256_update(&entropy_pool, (uint8_t *)&delta, sizeof(delta));
    prev = now;
  }
  uint64_t final_acc = acc;
  sha256_update(&entropy_pool, (uint8_t *)&final_acc, sizeof(final_acc));
#endif
}

/*
 * Mistura fontes de hardware diretas no pool. Usado tanto no boot
 * inicial quanto pelos paths de reseed (automatico e manual). Cada
 * iteracao mistura: ate 4 RDRAND quando disponivel + TSC fresco +
 * o contador de reseed (para garantir injetividade mesmo se hardware
 * der bytes identicos). Deve ser chamado dentro de irq_save/restore
 * pelo caller.
 */
static void mix_hardware_entropy(void) {
  if (has_rdrand()) {
    for (int i = 0; i < 4; ++i) {
      uint64_t hw = 0u;
      if (rdrand64(&hw)) {
        sha256_update(&entropy_pool, (uint8_t *)&hw, sizeof(hw));
      }
    }
  }
  uint64_t tsc = read_tsc();
  sha256_update(&entropy_pool, (uint8_t *)&tsc, sizeof(tsc));
  sha256_update(&entropy_pool, (uint8_t *)&reseed_counter,
                sizeof(reseed_counter));
}

void csprng_init(void) {
  if (initialized) {
    return;
  }
  sha256_init(&entropy_pool);
  /* Salt fixo + boot-marker bytes. O salt nao precisa ser segredo
   * (atacante com binario o conhece), mas separa o dominio do CSPRNG
   * de outros usos de SHA-256 e estabelece um estado inicial nao-zero
   * — importante porque RDRAND/TSC poderiam todos retornar zero em
   * cenarios adversariais e o pool ficaria a partir do estado IV
   * padrao do SHA-256, conhecido. */
  static const uint8_t k_boot_salt[] = "CAPYOS_Initial_Salt_v1";
  sha256_update(&entropy_pool, k_boot_salt, sizeof(k_boot_salt));
  static const uint8_t k_boot_marker[] = {
      0xCA, 0x97, 0x05, 0x21, 0xAB, 0xCD, 0xEF, 0x42,
      0x13, 0x37, 0xC0, 0xDE, 0xFE, 0xED, 0xFA, 0xCE};
  sha256_update(&entropy_pool, k_boot_marker, sizeof(k_boot_marker));

  /* Endereco do entropy_pool tambem entra: em kernels com KASLR sera
   * randomizado por boot; sem KASLR ainda contribui via versionamento
   * binario (uma rebuild em endereco diferente diverge o pool). */
  uintptr_t pool_addr = (uintptr_t)&entropy_pool;
  sha256_update(&entropy_pool, (uint8_t *)&pool_addr, sizeof(pool_addr));

  mix_hardware_entropy();
  tsc_jitter_collect();

  reseed_counter = 0u;
  bytes_since_reseed = 0u;
  initialized = 1;
}

void csprng_feed_entropy(uint32_t data) {
  unsigned long flags;
  if (!initialized) {
    csprng_init();
  }
  flags = irq_save();
  sha256_update(&entropy_pool, (uint8_t *)&data, sizeof(data));
  reseed_counter++;
  irq_restore(flags);
}

void csprng_feed_entropy_buffer(const void *data, size_t len) {
  if (!data || len == 0u) {
    return;
  }
  unsigned long flags;
  if (!initialized) {
    csprng_init();
  }
  flags = irq_save();
  sha256_update(&entropy_pool, (const uint8_t *)data, len);
  reseed_counter++;
  irq_restore(flags);
}

void csprng_reseed(void) {
  unsigned long flags;
  if (!initialized) {
    csprng_init();
    return;
  }
  flags = irq_save();
  mix_hardware_entropy();
  bytes_since_reseed = 0u;
  reseed_counter++;
  irq_restore(flags);
}

void csprng_get_bytes(void *buf, size_t len) {
  if (!initialized) {
    csprng_init();
  }
  if (!buf || len == 0u) {
    return;
  }
  uint8_t *p = (uint8_t *)buf;
  unsigned long flags = irq_save();

  while (len > 0u) {
    /* Reseed proativo: se ja emitimos CSPRNG_RESEED_INTERVAL_BYTES
     * desde o ultimo reseed, mistura fontes de hardware ANTES de
     * produzir o proximo bloco. Limita a janela de comprometimento
     * caso atacante consiga inferir parte do pool — apos reseed, o
     * pool inclui RDRAND/TSC frescos que nao estavam no estado
     * inferido. Custo: uma chamada a RDRAND (~50 ciclos) + uma a
     * RDTSC (~30 ciclos) por 64 KiB emitidos, desprezivel. */
    if (bytes_since_reseed >= CSPRNG_RESEED_INTERVAL_BYTES) {
      mix_hardware_entropy();
      bytes_since_reseed = 0u;
    }

    uint8_t digest[32];
    /* Snapshot do pool. Importante: a copia carrega o estado interno
     * inteiro do SHA-256 (state[] + data[] + bit count). Apos
     * finalizar a copia, `temp_ctx.state[]` IS o digest emitido e
     * `temp_ctx.data[]` carrega o ultimo bloco padded — ambos
     * sensiveis, devem ser zerados antes da proxima iteracao ou do
     * retorno da funcao. */
    struct sha256_ctx temp_ctx = entropy_pool;

    sha256_update(&temp_ctx, (uint8_t *)&reseed_counter,
                  sizeof(reseed_counter));
    sha256_final(&temp_ctx, digest);

    /* Forward secrecy: realimenta o digest emitido no pool. Apos
     * isso, o estado anterior do pool nao e mais recuperavel pelo
     * proximo snapshot — atacante que extraia o pool agora nao
     * consegue derivar o digest anterior. */
    sha256_update(&entropy_pool, digest, 32u);
    reseed_counter++;

    size_t to_copy = (len < 32u) ? len : 32u;
    for (size_t i = 0u; i < to_copy; ++i) {
      *p++ = digest[i];
    }
    len -= to_copy;
    bytes_since_reseed += to_copy;

    /* Wipe hygiene: temp_ctx (que carrega o digest + bloco padded
     * derivado do pool) e digest local sao zerados via primitivas
     * volatile-safe que o compilador nao pode eliminar como
     * dead-store. Sem isso, um dump de panic ou info-leak via
     * leitura de stack expoe o output do CSPRNG. */
    sha256_clear(&temp_ctx);
    for (volatile int i = 0; i < 32; ++i) {
      digest[i] = 0u;
    }
  }

  irq_restore(flags);
}

uint32_t csprng_next_u32(void) {
  uint32_t val = 0u;
  csprng_get_bytes(&val, sizeof(val));
  return val;
}
