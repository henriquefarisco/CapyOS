#include "services/update_payload_cache.h"

static const uint8_t magic[8] = {'C', 'A', 'P', 'Y', 'U', 'C', '0', '1'};

static uint32_t load32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void store32(uint8_t *p, uint32_t n) {
    for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(n >> (i * 8));
}

static int owned_path(const char *path) {
    const char expected[] = "/system/update/payload.bin";
    if (!path) return 0;
    for (size_t i = 0; i < sizeof(expected); ++i)
        if (path[i] != expected[i]) return 0;
    return 1;
}

static void part_path(char out[UPDATE_AGENT_PATH_MAX], unsigned part) {
    const char base[] = "/system/update/payload.bin.part";
    size_t i;
    for (i = 0; i < sizeof(base) - 1; ++i) out[i] = base[i];
    out[i++] = (char)('0' + part);
    out[i] = 0;
}

int update_cache_remove(const struct update_cache_io *io, const char *path) {
    if (!io || !io->remove || !owned_path(path)) return -1;
    /* Hide the descriptor before touching parts. No boot-slot activation can
     * consume a mixed generation; callers must reverify the signed full hash. */
    int rc = io->remove(path);
    for (unsigned i = 0; i < UPDATE_CACHE_MAX_PARTS; ++i) {
        char part[UPDATE_AGENT_PATH_MAX];
        part_path(part, i);
        if (io->remove(part) != 0) rc = -1;
    }
    return rc;
}

int update_cache_write(const struct update_cache_io *io, const char *path,
                       const uint8_t *data, size_t size) {
    if (!io || !io->write || !data || !owned_path(path) || !size ||
        size > UPDATE_AGENT_PAYLOAD_MAX_BYTES) return -1;
    if (update_cache_remove(io, path) != 0) return -1;
    if (size <= UPDATE_CACHE_SINGLE_BYTES) return io->write(path, data, size);
    uint8_t descriptor[UPDATE_CACHE_DESCRIPTOR_BYTES];
    uint32_t count = (uint32_t)((size + UPDATE_CACHE_PART_BYTES - 1u) /
                                UPDATE_CACHE_PART_BYTES);
    for (unsigned i = 0; i < sizeof(magic); ++i) descriptor[i] = magic[i];
    store32(descriptor + 8, (uint32_t)size);
    store32(descriptor + 12, count);
    store32(descriptor + 16, UPDATE_CACHE_PART_BYTES);
    size_t done = 0;
    for (unsigned i = 0; i < count; ++i) {
        char part[UPDATE_AGENT_PATH_MAX];
        part_path(part, i);
        size_t n = size - done;
        if (n > UPDATE_CACHE_PART_BYTES) n = UPDATE_CACHE_PART_BYTES;
        if (io->write(part, data + done, n) != 0) {
            (void)update_cache_remove(io, path);
            return -1;
        }
        done += n;
    }
    /* Commit last. An interrupted download has no readable descriptor. */
    if (io->write(path, descriptor, sizeof(descriptor)) != 0) {
        (void)update_cache_remove(io, path);
        return -1;
    }
    return 0;
}

int update_cache_read(const struct update_cache_io *io, const char *path,
                      uint8_t *buffer, size_t capacity, size_t *out_len) {
    size_t n = 0;
    if (out_len) *out_len = 0;
    if (!io || !io->read || !owned_path(path) || !buffer || !capacity ||
        capacity > UPDATE_AGENT_PAYLOAD_MAX_BYTES || !out_len ||
        io->read(path, buffer, capacity, &n) != 0 || !n || n > capacity)
        return -1;
    int segmented = n >= sizeof(magic);
    for (unsigned i = 0; segmented && i < sizeof(magic); ++i)
        if (buffer[i] != magic[i]) segmented = 0;
    if (!segmented) {
        if (n > UPDATE_CACHE_SINGLE_BYTES) return -1;
        *out_len = n;
        return 0;
    }
    if (n != UPDATE_CACHE_DESCRIPTOR_BYTES) return -1;
    uint32_t total = load32(buffer + 8), count = load32(buffer + 12);
    uint32_t chunk = load32(buffer + 16);
    if (total <= UPDATE_CACHE_SINGLE_BYTES || total > capacity ||
        chunk != UPDATE_CACHE_PART_BYTES || count > UPDATE_CACHE_MAX_PARTS ||
        count != (total + UPDATE_CACHE_PART_BYTES - 1u) / UPDATE_CACHE_PART_BYTES)
        return -1;
    size_t done = 0;
    for (unsigned i = 0; i < count; ++i) {
        char part[UPDATE_AGENT_PATH_MAX];
        part_path(part, i);
        size_t expected = total - done;
        if (expected > UPDATE_CACHE_PART_BYTES) expected = UPDATE_CACHE_PART_BYTES;
        n = 0;
        if (io->read(part, buffer + done, expected, &n) != 0 || n != expected)
            return -1;
        done += n;
    }
    *out_len = done;
    return 0;
}
