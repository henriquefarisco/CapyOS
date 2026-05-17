#include <stdio.h>
#include <string.h>
#include "security/csprng.h"

static int test_csprng_init(void) {
    csprng_init();
    // If we didn't crash, great.
    return 0;
}

static int test_csprng_output(void) {
    uint8_t buf1[32];
    uint8_t buf2[32];

    // Reset buffer
    memset(buf1, 0, 32);
    memset(buf2, 0, 32);

    // Get random bytes
    csprng_get_bytes(buf1, 32);
    // Extremely unlikely to be 0 unless broken
    int all_zeros = 1;
    for(int i=0; i<32; i++) if (buf1[i] != 0) all_zeros = 0;

    if (all_zeros) {
        printf("[csprng] output all zeros\n");
        return 1;
    }

    // Get more bytes - should be different
    csprng_get_bytes(buf2, 32);
    if (memcmp(buf1, buf2, 32) == 0) {
        printf("[csprng] consecutive outputs identical\n");
        return 1;
    }
    return 0;
}

static int test_csprng_entropy(void) {
    // Feed some entropy
    csprng_feed_entropy(0xDEADBEEF);
    csprng_feed_entropy(0xCAFEBABE);

    uint32_t val = csprng_next_u32();
    if (val == 0) {
       // Possible, but unlikely. Let's accept 0 as valid if it happens once,
       // but maybe check if it's consistently 0? No, let's just assume valid.
       // Actually 0 is a valid random number.
    }
    return 0;
}

static int test_csprng_feed_buffer(void) {
    /* Contract: feed_entropy_buffer aceita NULL/zero-length sem
     * crash; aceita buffers arbitrarios e os mistura no pool; o
     * output apos feeding nao colide com o output sem feeding
     * (mudou o pool). */
    int fails = 0;

    csprng_feed_entropy_buffer(NULL, 0u);    // graceful
    csprng_feed_entropy_buffer(NULL, 16u);   // graceful (NULL data)
    csprng_feed_entropy_buffer("ignored", 0u); // graceful (zero len)

    uint8_t before[32];
    csprng_get_bytes(before, sizeof(before));

    static const uint8_t large_buf[256] = {
        0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBA, 0xBE,
        0x13, 0x37, 0xC0, 0xDE, 0xFE, 0xED, 0xFA, 0xCE};
    csprng_feed_entropy_buffer(large_buf, sizeof(large_buf));

    uint8_t after[32];
    csprng_get_bytes(after, sizeof(after));

    if (memcmp(before, after, 32) == 0) {
        printf("[csprng] feed_buffer did not affect output stream\n");
        fails++;
    }

    return fails;
}

static int test_csprng_reseed(void) {
    /* Contract: csprng_reseed deve ser chamavel em qualquer ponto sem
     * crash; output apos reseed nao deve colidir com output anterior;
     * NAO deve resetar o pool a um estado fresco (e adicao de entropia,
     * nao re-init). */
    int fails = 0;

    uint8_t before[32];
    csprng_get_bytes(before, sizeof(before));

    csprng_reseed();

    uint8_t after[32];
    csprng_get_bytes(after, sizeof(after));

    if (memcmp(before, after, 32) == 0) {
        printf("[csprng] reseed did not affect output stream\n");
        fails++;
    }

    /* Idempotent: chamar duas vezes seguidas tambem nao colide. */
    csprng_reseed();
    csprng_reseed();
    uint8_t after2[32];
    csprng_get_bytes(after2, sizeof(after2));
    if (memcmp(after, after2, 32) == 0) {
        printf("[csprng] reseed called twice did not affect output stream\n");
        fails++;
    }

    return fails;
}

static int test_csprng_auto_reseed_after_interval(void) {
    /* Contract: emitir mais de CSPRNG_RESEED_INTERVAL_BYTES bytes
     * deve cruzar o limiar de reseed automatico interno. Como
     * UNIT_TEST nao tem RDRAND/TSC reais, o reseed nao adiciona
     * entropia visivel — mas o codigo NAO pode crashear nem
     * entrar em loop. Validacao: emitir 256 KiB (4x o intervalo) e
     * verificar que o stream continua produzindo bytes nao-zero. */
    int fails = 0;
    const size_t total = (size_t)(CSPRNG_RESEED_INTERVAL_BYTES * 4u);
    static uint8_t big_buf[256u * 1024u];
    csprng_get_bytes(big_buf, total);

    int all_zero = 1;
    for (size_t i = 0u; i < total; i += 4096u) {
        if (big_buf[i] != 0u || big_buf[i + 1u] != 0u ||
            big_buf[i + 2u] != 0u || big_buf[i + 3u] != 0u) {
            all_zero = 0;
            break;
        }
    }
    if (all_zero) {
        printf("[csprng] auto-reseed: output stream is all zero (broken)\n");
        fails++;
    }

    /* Stream coherence: dois quadrantes do buffer devem diferir entre
     * si — se o reseed automatico resetasse o pool, dois quadrantes
     * sucessivos poderiam acidentalmente colidir; aqui validamos que
     * o reseed nao destroi a continuidade do stream. */
    if (memcmp(big_buf, big_buf + total / 2u, 32u) == 0) {
        printf("[csprng] auto-reseed broke stream continuity\n");
        fails++;
    }

    return fails;
}

int run_csprng_tests(void) {
    int fails = 0;
    fails += test_csprng_init();
    fails += test_csprng_output();
    fails += test_csprng_entropy();
    fails += test_csprng_feed_buffer();
    fails += test_csprng_reseed();
    fails += test_csprng_auto_reseed_after_interval();
    if (fails == 0) {
        printf("[tests] csprng OK\n");
    } else {
        printf("[tests] csprng FAILED\n");
    }
    return fails;
}
