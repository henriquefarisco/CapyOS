#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fs/block.h"
#include "memory/kmem.h"

struct mem_backend {
    uint32_t block_size;
    uint32_t block_count;
    uint8_t *data;
};

static int mem_read(void *ctx, uint32_t block_no, void *buffer) {
    struct mem_backend *m = (struct mem_backend *)ctx;
    if (block_no >= m->block_count) {
        return -1;
    }
    memcpy(buffer, m->data + block_no * m->block_size, m->block_size);
    return 0;
}

static int mem_write(void *ctx, uint32_t block_no, const void *buffer) {
    struct mem_backend *m = (struct mem_backend *)ctx;
    if (block_no >= m->block_count) {
        return -1;
    }
    memcpy(m->data + block_no * m->block_size, buffer, m->block_size);
    return 0;
}

static struct block_device_ops g_mem_ops = {
    .read_block = mem_read,
    .write_block = mem_write,
};

static void mem_backend_init(struct mem_backend *m, uint32_t block_size, uint32_t block_count) {
    m->block_size = block_size;
    m->block_count = block_count;
    m->data = (uint8_t *)calloc(block_count, block_size);
}

static void mem_backend_free(struct mem_backend *m) {
    free(m->data);
    m->data = NULL;
}

static int test_offset_wrap_basic(void) {
    struct mem_backend mem;
    mem_backend_init(&mem, 512, 8);
    struct block_device base = { .name = "mem", .block_size = 512, .block_count = 8, .ctx = &mem, .ops = &g_mem_ops };

    struct block_device *view = block_offset_wrap(&base, 2, 4);
    if (!view) {
        printf("[offset] falha ao criar wrapper\n");
        mem_backend_free(&mem);
        return 1;
    }

    uint8_t pattern[512];
    memset(pattern, 0xA5, sizeof(pattern));
    if (block_device_write(view, 0, pattern) != 0) {
        printf("[offset] write falhou\n");
        return 1;
    }

    uint8_t check[512];
    memset(check, 0, sizeof(check));
    if (block_device_read(view, 0, check) != 0 || memcmp(check, pattern, sizeof(check)) != 0) {
        printf("[offset] leitura divergente\n");
        return 1;
    }

    uint8_t raw[512];
    mem_read(&mem, 2, raw);
    if (memcmp(raw, pattern, sizeof(raw)) != 0) {
        printf("[offset] backend nao recebeu dados esperados\n");
        return 1;
    }

    kfree(view->ctx);
    kfree(view);
    mem_backend_free(&mem);
    return 0;
}

static int test_chunk_wrap_rw(void) {
    struct mem_backend mem;
    mem_backend_init(&mem, 512, 16);
    struct block_device base = { .name = "mem", .block_size = 512, .block_count = 16, .ctx = &mem, .ops = &g_mem_ops };

    struct block_device *chunked = block_chunked_wrap(&base, 1024);
    if (!chunked) {
        printf("[chunk] falha ao criar wrapper 1KiB\n");
        mem_backend_free(&mem);
        return 1;
    }

    uint8_t pattern[1024];
    for (size_t i = 0; i < sizeof(pattern); ++i) {
        pattern[i] = (uint8_t)(i & 0xFF);
    }
    if (block_device_write(chunked, 1, pattern) != 0) {
        printf("[chunk] write falhou\n");
        return 1;
    }

    uint8_t raw0[512], raw1[512];
    mem_read(&mem, 2, raw0);
    mem_read(&mem, 3, raw1);
    if (memcmp(raw0, pattern, 512) != 0 || memcmp(raw1, pattern + 512, 512) != 0) {
        printf("[chunk] write nao espalhou pelos blocos base\n");
        return 1;
    }

    uint8_t readback[1024];
    memset(readback, 0, sizeof(readback));
    if (block_device_read(chunked, 1, readback) != 0 || memcmp(readback, pattern, sizeof(readback)) != 0) {
        printf("[chunk] leitura reagrupada falhou\n");
        return 1;
    }

    kfree(chunked->ctx);
    kfree(chunked);
    mem_backend_free(&mem);
    return 0;
}

static int test_chunk_offset_combo(void) {
    struct mem_backend mem;
    mem_backend_init(&mem, 512, 4096);
    struct block_device base = { .name = "disk", .block_size = 512, .block_count = 4096, .ctx = &mem, .ops = &g_mem_ops };

    struct block_device *offset = block_offset_wrap(&base, 2048, 1024);
    struct block_device *chunked = block_chunked_wrap(offset, 4096);
    if (!offset || !chunked) {
        printf("[combo] wrappers nao criados\n");
        return 1;
    }

    uint8_t payload[4096];
    memset(payload, 0x3C, sizeof(payload));
    if (block_device_write(chunked, 1, payload) != 0) {
        printf("[combo] write falhou\n");
        return 1;
    }

    uint8_t raw_head[512];
    mem_read(&mem, 2048 + 8, raw_head);
    if (raw_head[0] != 0x3C) {
        printf("[combo] dados nao chegaram no offset esperado\n");
        return 1;
    }

    uint8_t readback[4096];
    memset(readback, 0, sizeof(readback));
    if (block_device_read(chunked, 1, readback) != 0 || memcmp(readback, payload, sizeof(readback)) != 0) {
        printf("[combo] leitura apos offset+chunk divergiu\n");
        return 1;
    }

    kfree(chunked->ctx);
    kfree(chunked);
    kfree(offset->ctx);
    kfree(offset);
    mem_backend_free(&mem);
    return 0;
}

int run_block_wrapper_tests(void) {
    int fails = 0;
    fails += test_offset_wrap_basic();
    fails += test_chunk_wrap_rw();
    fails += test_chunk_offset_combo();
    if (fails == 0) {
        printf("[tests] block_wrappers OK\n");
    }
    return fails;
}
