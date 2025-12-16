#ifndef BOOT_WRITER_H
#define BOOT_WRITER_H

#include <stdint.h>
#include <stddef.h>
#include "boot/boot_manifest.h"
#include "fs/storage/partition.h"
#include "fs/block.h"

struct boot_payload {
    const uint8_t *data;
    uint32_t size;
};

struct boot_payload_set {
    struct boot_payload stage1;
    struct boot_payload stage2;
    struct boot_payload kernel_main;     /* ELF */
    struct boot_payload kernel_recovery; /* ELF (fallback) */
};

/* Retorna payloads embarcados no binário (gerados em build/generated/boot_payloads.h). */
struct boot_payload_set boot_embedded_payloads(void);

/* Prepara setor 0 combinando stage1 + partições e patches de LBA/size. */
int bootwriter_write_stage1(struct block_device *disk,
                            const struct mbr_partition *boot_part,
                            uint32_t stage2_lba,
                            uint32_t stage2_sectors,
                            const struct boot_payload *stage1);

/* Escreve stage2 + manifest + kernels na partição BOOT. */
int bootwriter_write_payloads(struct block_device *disk,
                              const struct mbr_partition *boot_part,
                              const struct boot_payload_set *payloads);

#endif
