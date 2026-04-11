/* boot_manifest.c: on-disk manifest helpers and checksum used by stage2. */
#include "boot/boot_manifest.h"

void boot_manifest_init(struct boot_manifest *m) {
    if (!m) {
        return;
    }
    m->magic = BOOT_MANIFEST_MAGIC;
    m->version = BOOT_MANIFEST_VERSION;
    m->entry_count = 0;
    m->reserved = 0;
    for (size_t i = 0; i < sizeof(m->entries) / sizeof(m->entries[0]); ++i) {
        m->entries[i].type = 0;
        m->entries[i].lba_start = 0;
        m->entries[i].sector_count = 0;
        m->entries[i].checksum32 = 0;
        m->entries[i].reserved = 0;
    }
}

int boot_manifest_add(struct boot_manifest *m, uint32_t type, uint32_t lba, uint32_t sectors, uint32_t checksum32) {
    if (!m) {
        return -1;
    }
    if (m->entry_count >= (sizeof(m->entries) / sizeof(m->entries[0]))) {
        return -1;
    }
    struct boot_manifest_entry *e = &m->entries[m->entry_count];
    e->type = type;
    e->lba_start = lba;
    e->sector_count = sectors;
    e->checksum32 = checksum32;
    e->reserved = 0;
    m->entry_count++;
    return 0;
}

uint32_t boot_checksum32(const uint8_t *data, size_t len) {
    if (!data || len == 0) {
        return 0;
    }
    uint32_t sum = 0;
    size_t i = 0;
    while (i + 4 <= len) {
        uint32_t v = (uint32_t)data[i] |
                     ((uint32_t)data[i + 1] << 8) |
                     ((uint32_t)data[i + 2] << 16) |
                     ((uint32_t)data[i + 3] << 24);
        sum += v;
        i += 4;
    }
    while (i < len) {
        sum += data[i];
        ++i;
    }
    return sum;
}
