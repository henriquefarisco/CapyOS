#ifndef SERVICES_UPDATE_PAYLOAD_CACHE_H
#define SERVICES_UPDATE_PAYLOAD_CACHE_H

#include "services/update_agent.h"
#include "fs/capyfs.h"

#define UPDATE_CACHE_PART_BYTES (2u * 1024u * 1024u)
#define UPDATE_CACHE_MAX_PARTS 4u
#define UPDATE_CACHE_DESCRIPTOR_BYTES 20u
/* Existing CapyFS v2: twelve direct blocks and one indirect block. */
#define UPDATE_CACHE_SINGLE_BYTES ((12u + CAPYFS_BLOCK_SIZE / sizeof(uint32_t)) * CAPYFS_BLOCK_SIZE)

struct update_cache_io {
    update_agent_read_bytes_fn read;
    update_agent_write_bytes_fn write;
    /* Removing a missing service-owned cache file must succeed. */
    update_agent_remove_file_fn remove;
};

int update_cache_read(const struct update_cache_io *io, const char *path,
                      uint8_t *buffer, size_t capacity, size_t *out_len);
int update_cache_write(const struct update_cache_io *io, const char *path,
                       const uint8_t *data, size_t size);
int update_cache_remove(const struct update_cache_io *io, const char *path);

#endif
