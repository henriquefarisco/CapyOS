#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "boot/boot_writer.h"
#include "boot/boot_manifest.h"
#include "fs/block.h"

struct mem_disk {
    uint32_t block_size;
    uint32_t block_count;
    uint8_t *data;
};

static int mem_read(void *ctx, uint32_t block_no, void *buffer) {
    struct mem_disk *m = (struct mem_disk *)ctx;
    if (block_no >= m->block_count) return -1;
    memcpy(buffer, m->data + block_no * m->block_size, m->block_size);
    return 0;
}
static int mem_write(void *ctx, uint32_t block_no, const void *buffer) {
    struct mem_disk *m = (struct mem_disk *)ctx;
    if (block_no >= m->block_count) return -1;
    memcpy(m->data + block_no * m->block_size, buffer, m->block_size);
    return 0;
}

static struct block_device_ops mem_ops = {
    .read_block = mem_read,
    .write_block = mem_write,
};

static int test_bootwriter_basic(void) {
    struct mem_disk mem;
    mem.block_size = 512;
    mem.block_count = 8192;
    mem.data = (uint8_t *)calloc(mem.block_count, mem.block_size);
    if (!mem.data) return 1;

    struct block_device disk = {
        .name = "mem",
        .block_size = 512,
        .block_count = mem.block_count,
        .ctx = &mem,
        .ops = &mem_ops,
    };

    struct boot_payload_set set = {0};
    uint8_t stage1[512]; memset(stage1, 0, sizeof(stage1));
    /* place placeholder little-endian */
    stage1[0] = 0xEF; stage1[1] = 0xBE; stage1[2] = 0xAD; stage1[3] = 0xDE;
    stage1[510] = 0x55; stage1[511] = 0xAA;
    uint8_t stage2[1024]; memset(stage2, 0, sizeof(stage2));
    stage2[0] = 0xEF; stage2[1] = 0xBE; stage2[2] = 0xAD; stage2[3] = 0xDE;
    uint8_t kmain[600]; for (size_t i=0;i<sizeof(kmain);++i) kmain[i]=(uint8_t)i;
    uint8_t krec[512]; memset(krec, 0xAA, sizeof(krec));

    set.stage1.data = stage1; set.stage1.size = sizeof(stage1);
    set.stage2.data = stage2; set.stage2.size = sizeof(stage2);
    set.kernel_main.data = kmain; set.kernel_main.size = sizeof(kmain);
    set.kernel_recovery.data = krec; set.kernel_recovery.size = sizeof(krec);

    struct mbr_partition bootp = {0};
    bootp.lba_start = 2048;
    bootp.sector_count = 4096;

    int rc = bootwriter_write_payloads(&disk, &bootp, &set);
    if (rc != 0) {
        printf("[bootwriter] retorno %d\n", rc);
        free(mem.data);
        return 1;
    }
    uint8_t mbr[512];
    mem_read(&mem, 0, mbr);
    uint32_t patched = (uint32_t)mbr[0] | ((uint32_t)mbr[1]<<8) | ((uint32_t)mbr[2]<<16) | ((uint32_t)mbr[3]<<24);
    if (patched != bootp.lba_start) {
        printf("[bootwriter] stage1 nao recebeu LBA (0x%x)\n", patched);
        free(mem.data);
        return 1;
    }
    uint8_t manifest_buf[512];
    mem_read(&mem, bootp.lba_start + (set.stage2.size+511)/512, manifest_buf);
    uint32_t magic = *(uint32_t *)manifest_buf;
    if (magic != BOOT_MANIFEST_MAGIC) {
        printf("[bootwriter] manifest magic invalido\n");
        free(mem.data);
        return 1;
    }
    struct boot_manifest *man = (struct boot_manifest *)manifest_buf;
    if (man->entry_count != 2) {
        printf("[bootwriter] manifest entry_count=%u\n", man->entry_count);
        free(mem.data);
        return 1;
    }
    free(mem.data);
    return 0;
}

int run_boot_writer_tests(void) {
    int fails = 0;
    fails += test_bootwriter_basic();
    if (fails == 0) {
        printf("[tests] boot_writer OK\n");
    }
    return fails;
}
