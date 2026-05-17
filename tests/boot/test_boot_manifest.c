#include <stdio.h>
#include <string.h>

#include "boot/boot_manifest.h"

static int test_manifest_add_and_checksum(void) {
    struct boot_manifest m;
    boot_manifest_init(&m);
    if (m.magic != BOOT_MANIFEST_MAGIC || m.version != BOOT_MANIFEST_VERSION) {
        printf("[manifest] magic/version incorretos\n");
        return 1;
    }
    uint8_t data[8] = {1,2,3,4,5,6,7,8};
    uint32_t sum = boot_checksum32(data, sizeof(data));
    if (sum != 201984006u) {
        printf("[manifest] checksum inesperado %u\n", sum);
        return 1;
    }
    if (boot_manifest_add(&m, BOOT_ENTRY_NORMAL, 2048, 4, sum) != 0) {
        printf("[manifest] falha ao adicionar entrada\n");
        return 1;
    }
    if (m.entry_count != 1 || m.entries[0].lba_start != 2048 || m.entries[0].checksum32 != sum) {
        printf("[manifest] entrada inconsistente\n");
        return 1;
    }
    return 0;
}

int run_boot_manifest_tests(void) {
    int fails = 0;
    fails += test_manifest_add_and_checksum();
    if (fails == 0) {
        printf("[tests] boot_manifest OK\n");
    }
    return fails;
}
