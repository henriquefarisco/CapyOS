#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fs/block.h"
#include "fs/storage/partition.h"

struct mem_backend_p {
    uint32_t block_size;
    uint32_t block_count;
    uint8_t *data;
};

static int mem_read_p(void *ctx, uint32_t block_no, void *buffer) {
    struct mem_backend_p *m = (struct mem_backend_p *)ctx;
    if (block_no >= m->block_count) {
        return -1;
    }
    memcpy(buffer, m->data + block_no * m->block_size, m->block_size);
    return 0;
}

static int mem_write_p(void *ctx, uint32_t block_no, const void *buffer) {
    struct mem_backend_p *m = (struct mem_backend_p *)ctx;
    if (block_no >= m->block_count) {
        return -1;
    }
    memcpy(m->data + block_no * m->block_size, buffer, m->block_size);
    return 0;
}

static struct block_device_ops g_mem_ops_p = {
    .read_block = mem_read_p,
    .write_block = mem_write_p,
};

static void fill_mbr(uint8_t *mbr, uint32_t start, uint32_t sectors, uint8_t type) {
    memset(mbr, 0, 512);
    mbr[510] = 0x55;
    mbr[511] = 0xAA;
    uint8_t *p = &mbr[446 + 1 * 16];
    p[0] = 0x00;
    p[1] = p[2] = p[3] = p[5] = p[6] = p[7] = 0xFF;
    p[4] = type;
    p[8] = (uint8_t)(start & 0xFF);
    p[9] = (uint8_t)((start >> 8) & 0xFF);
    p[10] = (uint8_t)((start >> 16) & 0xFF);
    p[11] = (uint8_t)((start >> 24) & 0xFF);
    p[12] = (uint8_t)(sectors & 0xFF);
    p[13] = (uint8_t)((sectors >> 8) & 0xFF);
    p[14] = (uint8_t)((sectors >> 16) & 0xFF);
    p[15] = (uint8_t)((sectors >> 24) & 0xFF);
}

static int test_mbr_parse_success(void) {
    struct mem_backend_p mem;
    mem.block_size = 512;
    mem.block_count = 131072; // ~64 MiB
    mem.data = (uint8_t *)calloc(mem.block_count, mem.block_size);

    uint8_t mbr[512];
    fill_mbr(mbr, 2048, 40000, 0x83);
    memcpy(mem.data, mbr, 512);

    struct block_device dev = { .name = "mem", .block_size = 512, .block_count = 131072, .ctx = &mem, .ops = &g_mem_ops_p };
    struct mbr_partition part;
    if (mbr_read_partition(&dev, 1, &part) != 0) {
        printf("[mbr] parse falhou\n");
        free(mem.data);
        return 1;
    }
    if (part.lba_start != 2048 || part.sector_count != 40000 || part.type != 0x83) {
        printf("[mbr] valores inesperados (%u, %u, %u)\n", part.lba_start, part.sector_count, part.type);
        free(mem.data);
        return 1;
    }

    free(mem.data);
    return 0;
}

static int test_mbr_invalid_signature(void) {
    struct mem_backend_p mem;
    mem.block_size = 512;
    mem.block_count = 64;
    mem.data = (uint8_t *)calloc(mem.block_count, mem.block_size);

    uint8_t mbr[512];
    memset(mbr, 0, sizeof(mbr));
    memcpy(mem.data, mbr, 512);

    struct block_device dev = { .name = "mem", .block_size = 512, .block_count = 64, .ctx = &mem, .ops = &g_mem_ops_p };
    struct mbr_partition part;
    int rc = mbr_read_partition(&dev, 1, &part);
    free(mem.data);
    if (rc == 0) {
        printf("[mbr] aceitou assinatura invalida\n");
        return 1;
    }
    return 0;
}

static int test_mbr_zero_sectors(void) {
    struct mem_backend_p mem;
    mem.block_size = 512;
    mem.block_count = 64;
    mem.data = (uint8_t *)calloc(mem.block_count, mem.block_size);

    uint8_t mbr[512];
    fill_mbr(mbr, 4096, 0, 0x83);
    memcpy(mem.data, mbr, 512);

    struct block_device dev = { .name = "mem", .block_size = 512, .block_count = 64, .ctx = &mem, .ops = &g_mem_ops_p };
    struct mbr_partition part;
    int rc = mbr_read_partition(&dev, 1, &part);
    free(mem.data);
    if (rc == 0) {
        printf("[mbr] aceitou particao com 0 setores\n");
        return 1;
    }
    return 0;
}

int run_partition_tests(void) {
    int fails = 0;
    fails += test_mbr_parse_success();
    fails += test_mbr_invalid_signature();
    fails += test_mbr_zero_sectors();
    if (fails == 0) {
        printf("[tests] partition helpers OK\n");
    }
    return fails;
}
