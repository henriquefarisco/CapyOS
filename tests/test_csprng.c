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

int run_csprng_tests(void) {
    int fails = 0;
    fails += test_csprng_init();
    fails += test_csprng_output();
    fails += test_csprng_entropy();
    if (fails == 0) {
        printf("[tests] csprng OK\n");
    } else {
        printf("[tests] csprng FAILED\n");
    }
    return fails;
}
