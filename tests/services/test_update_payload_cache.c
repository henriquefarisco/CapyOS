#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "services/update_payload_cache.h"

static const char *paths[] = {"/system/update/payload.bin",
    "/system/update/payload.bin.part0", "/system/update/payload.bin.part1",
    "/system/update/payload.bin.part2", "/system/update/payload.bin.part3"};
static uint8_t *files[5];
static size_t sizes[5];
static int failed_write = -1, writes, errors;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "cache line %d: %s\n", __LINE__, #c); ++errors; } } while (0)

static int index_of(const char *path) {
    for (int i = 0; i < 5; ++i) if (strcmp(paths[i], path) == 0) return i;
    return -1;
}
static int read_file(const char *path, uint8_t *out, size_t cap, size_t *len) {
    int i = index_of(path);
    if (i < 0 || !files[i] || sizes[i] > cap) return -1;
    memcpy(out, files[i], sizes[i]);
    *len = sizes[i];
    return 0;
}
static int remove_file(const char *path) {
    int i = index_of(path);
    if (i < 0) return -1;
    free(files[i]); files[i] = NULL; sizes[i] = 0;
    return 0;
}
static int write_file(const char *path, const uint8_t *bytes, size_t len) {
    int i = index_of(path);
    CHECK(len <= UPDATE_CACHE_SINGLE_BYTES);
    if (i < 0 || writes++ == failed_write || len > UPDATE_CACHE_SINGLE_BYTES) return -1;
    uint8_t *copy = malloc(len);
    if (!copy) return -1;
    memcpy(copy, bytes, len);
    free(files[i]); files[i] = copy; sizes[i] = len;
    return 0;
}
static const struct update_cache_io io = {read_file, write_file, remove_file};

int main(void) {
    const size_t maximum = UPDATE_AGENT_PAYLOAD_MAX_BYTES;
    uint8_t *input = malloc(maximum), *output = malloc(maximum);
    if (!input || !output) { free(input); free(output); return 1; }
    for (size_t i = 0; i < maximum; ++i) input[i] = (uint8_t)(i * 61u + 13u);
    const size_t lengths[] = {1, UPDATE_CACHE_SINGLE_BYTES,
        UPDATE_CACHE_SINGLE_BYTES + 1, 7400072, UPDATE_AGENT_PAYLOAD_MAX_BYTES};
    for (size_t k = 0; k < sizeof(lengths) / sizeof(lengths[0]); ++k) {
        size_t size = lengths[k], got = 0;
        CHECK(update_cache_write(&io, paths[0], input, size) == 0);
        CHECK(update_cache_read(&io, paths[0], output, maximum, &got) == 0);
        CHECK(got == size && memcmp(input, output, size) == 0);
        CHECK(sizes[0] == (size <= UPDATE_CACHE_SINGLE_BYTES ? size : UPDATE_CACHE_DESCRIPTOR_BYTES));
        CHECK(update_cache_read(&io, paths[0], output, size - 1, &got) != 0);
        CHECK(got == 0);
    }
    CHECK(update_cache_write(&io, paths[0], input, 0) != 0);
    CHECK(update_cache_write(&io, paths[0], input, maximum + 1) != 0);
    CHECK(update_cache_write(&io, "/system/update/../other", input, maximum) != 0);
    CHECK(update_cache_remove(&io, "/Music/user.ogg") != 0);
    CHECK(update_cache_write(&io, paths[0], input, maximum) == 0);
    uint8_t descriptor[UPDATE_CACHE_DESCRIPTOR_BYTES];
    memcpy(descriptor, files[0], sizeof(descriptor));
    const unsigned changed[] = {8, 11, 12, 15, 16, 19};
    for (size_t k = 0; k < sizeof(changed) / sizeof(changed[0]); ++k) {
        size_t got = 0;
        files[0][changed[k]] ^= 0x80;
        CHECK(update_cache_read(&io, paths[0], output, maximum, &got) != 0);
        CHECK(got == 0);
        memcpy(files[0], descriptor, sizeof(descriptor));
    }
    size_t got = 0;
    sizes[0]--;
    CHECK(update_cache_read(&io, paths[0], output, maximum, &got) != 0);
    sizes[0]++;
    sizes[2]--;
    CHECK(update_cache_read(&io, paths[0], output, maximum, &got) != 0);
    sizes[2] += 2;
    CHECK(update_cache_read(&io, paths[0], output, maximum, &got) != 0);
    sizes[2]--;
    CHECK(remove_file(paths[2]) == 0);
    CHECK(update_cache_read(&io, paths[0], output, maximum, &got) != 0);
    for (int failure = 0; failure < 5; ++failure) {
        writes = 0; failed_write = failure;
        CHECK(update_cache_write(&io, paths[0], input, maximum) != 0);
        CHECK(update_cache_read(&io, paths[0], output, maximum, &got) != 0);
        for (int i = 0; i < 5; ++i) CHECK(files[i] == NULL);
    }
    failed_write = -1;
    CHECK(update_cache_write(&io, paths[0], input, maximum) == 0);
    CHECK(update_cache_write(&io, paths[0], input, 1) == 0);
    for (int i = 1; i < 5; ++i) CHECK(files[i] == NULL);
    CHECK(update_cache_remove(&io, paths[0]) == 0);
    CHECK(update_cache_remove(&io, paths[0]) == 0);
    free(input); free(output);
    if (!errors) puts("[ok] segmented cache: boundaries, exact bytes, malformed metadata, missing/truncated/oversized parts, write failures and cleanup");
    return errors != 0;
}
