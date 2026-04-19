#ifndef BOOT_MANIFEST_H
#define BOOT_MANIFEST_H

#include <stdint.h>
#include <stddef.h>

#define BOOT_MANIFEST_MAGIC 0x5442494E /* "NIBT" */
#define BOOT_MANIFEST_VERSION 1

enum boot_entry_type {
    BOOT_ENTRY_NORMAL = 1,
    BOOT_ENTRY_RECOVERY = 2,
};

struct boot_manifest_entry {
    uint32_t type;
    uint32_t lba_start;
    uint32_t sector_count;
    uint32_t checksum32;
    uint32_t reserved;
};

struct boot_manifest {
    uint32_t magic;
    uint32_t version;
    uint32_t entry_count;
    uint32_t reserved;
    struct boot_manifest_entry entries[4];
};

/* Inicializa manifest com magic/version e zera entradas. */
void boot_manifest_init(struct boot_manifest *m);

/* Define uma entrada e incrementa entry_count se ainda couber. Retorna 0 em sucesso. */
int boot_manifest_add(struct boot_manifest *m, uint32_t type, uint32_t lba, uint32_t sectors, uint32_t checksum32);

/* Calcula soma simples de 32 bits sobre buffer (mesmo algoritmo usado pelo stage2). */
uint32_t boot_checksum32(const uint8_t *data, size_t len);

#endif
