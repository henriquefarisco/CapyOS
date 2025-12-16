#include "boot/boot_writer.h"

#include "boot/boot_manifest.h"
#include "fs/block.h"
#include "fs/storage/partition.h"
#include "memory/kmem.h"

#define STAGE1_LBA_PLACEHOLDER 0xDEADBEEF
#define STAGE2_SIG_BYTES 0xDE, 0xAD, 0xBE, 0xEF

static int patch_u32(uint8_t *buf, size_t len, uint32_t placeholder, uint32_t value) {
    if (!buf || len < 4) {
        return -1;
    }
    size_t found = 0;
    for (size_t i = 0; i + 4 <= len; ++i) {
        uint32_t v = (uint32_t)buf[i] | ((uint32_t)buf[i + 1] << 8) |
                     ((uint32_t)buf[i + 2] << 16) | ((uint32_t)buf[i + 3] << 24);
        if (v == placeholder) {
            buf[i] = (uint8_t)(value & 0xFF);
            buf[i + 1] = (uint8_t)((value >> 8) & 0xFF);
            buf[i + 2] = (uint8_t)((value >> 16) & 0xFF);
            buf[i + 3] = (uint8_t)((value >> 24) & 0xFF);
            found++;
        }
    }
    return found > 0 ? 0 : -1;
}

static int patch_u16(uint8_t *buf, size_t len, uint16_t placeholder, uint16_t value) {
    if (!buf || len < 2) {
        return -1;
    }
    size_t found = 0;
    for (size_t i = 0; i + 2 <= len; ++i) {
        uint16_t v = (uint16_t)buf[i] | ((uint16_t)buf[i + 1] << 8);
        if (v == placeholder) {
            buf[i] = (uint8_t)(value & 0xFF);
            buf[i + 1] = (uint8_t)((value >> 8) & 0xFF);
            found++;
        }
    }
    return found > 0 ? 0 : -1;
}

static uint32_t sectors_for_size(uint32_t size) {
    return (size + 511) / 512;
}

static int write_sector_aligned(struct block_device *disk, uint32_t lba, const uint8_t *data, uint32_t size) {
    if (!disk || !data || size == 0) {
        return -1;
    }
    uint32_t sectors = sectors_for_size(size);
    uint8_t *tmp = (uint8_t *)kalloc(512);
    if (!tmp) return -1;
    uint32_t remaining = size;
    const uint8_t *ptr = data;
    for (uint32_t i = 0; i < sectors; ++i) {
        for (size_t j = 0; j < 512; ++j) {
            tmp[j] = (i * 512 + j < size) ? ptr[i * 512 + j] : 0;
        }
        if (block_device_write(disk, lba + i, tmp) != 0) {
            kfree(tmp);
            return -1;
        }
        remaining = (remaining > 512) ? (remaining - 512) : 0;
    }
    kfree(tmp);
    return 0;
}

int bootwriter_write_stage1(struct block_device *disk,
                            const struct mbr_partition *boot_part,
                            uint32_t stage2_lba,
                            uint32_t stage2_sectors,
                            const struct boot_payload *stage1) {
    if (!disk || !boot_part || !stage1 || !stage1->data || stage1->size < 512) {
        return -1;
    }
    uint8_t sector[512];
    for (size_t i = 0; i < 512; ++i) {
        sector[i] = stage1->data[i];
    }
    /* Preserva tabela de partições existente. */
    uint8_t mbr_existing[512];
    if (block_device_read(disk, 0, mbr_existing) != 0) {
        return -1;
    }
    for (size_t i = 0; i < 64; ++i) {
        sector[446 + i] = mbr_existing[446 + i];
    }
    /* Patch stage2 lba/size placeholders. */
    if (patch_u32(sector, sizeof(sector), STAGE1_LBA_PLACEHOLDER, stage2_lba) != 0) {
        /* fallback: try known marker 0xDEADBEEF little endian */
        if (patch_u32(sector, sizeof(sector), 0xEFBEADDEu, stage2_lba) != 0) {
            return -1;
        }
    }
    /* stage2_sectors placeholder is 0x0001 (word) */
    patch_u16(sector, sizeof(sector), 0x0001u, (uint16_t)stage2_sectors);
    sector[510] = 0x55;
    sector[511] = 0xAA;
    return block_device_write(disk, 0, sector);
}

int bootwriter_write_payloads(struct block_device *disk,
                              const struct mbr_partition *boot_part,
                              const struct boot_payload_set *payloads) {
    if (!disk || !boot_part || !payloads) {
        return -1;
    }
    if (!payloads->stage1.data || !payloads->stage2.data ||
        !payloads->kernel_main.data || !payloads->kernel_recovery.data) {
        return -1;
    }
    uint32_t stage2_lba = boot_part->lba_start;
    uint32_t stage2_sectors = sectors_for_size(payloads->stage2.size);
    uint32_t manifest_lba = stage2_lba + stage2_sectors;
    uint32_t kernel_main_lba = manifest_lba + 1;
    uint32_t kernel_main_sectors = sectors_for_size(payloads->kernel_main.size);
    uint32_t kernel_recovery_lba = kernel_main_lba + kernel_main_sectors;

    struct boot_manifest manifest;
    boot_manifest_init(&manifest);
    uint32_t csum_main = boot_checksum32(payloads->kernel_main.data, payloads->kernel_main.size);
    uint32_t csum_recovery = boot_checksum32(payloads->kernel_recovery.data, payloads->kernel_recovery.size);
    if (boot_manifest_add(&manifest, BOOT_ENTRY_NORMAL, kernel_main_lba, kernel_main_sectors, csum_main) != 0) {
        return -1;
    }
    if (boot_manifest_add(&manifest, BOOT_ENTRY_RECOVERY, kernel_recovery_lba, sectors_for_size(payloads->kernel_recovery.size), csum_recovery) != 0) {
        return -1;
    }

    /* Patch stage2 with LBA/size so it can find manifest. */
    uint32_t stage2_bytes = payloads->stage2.size;
    uint32_t stage2_padded = stage2_sectors * 512;
    uint8_t *stage2_copy = (uint8_t *)kalloc(stage2_padded);
    if (!stage2_copy) {
        return -1;
    }
    for (uint32_t i = 0; i < stage2_padded; ++i) {
        stage2_copy[i] = (i < stage2_bytes) ? payloads->stage2.data[i] : 0;
    }
    if (patch_u32(stage2_copy, stage2_padded, STAGE1_LBA_PLACEHOLDER, stage2_lba) != 0) {
        if (patch_u32(stage2_copy, stage2_padded, 0xEFBEADDEu, stage2_lba) != 0) {
            kfree(stage2_copy);
            return -1;
        }
    }
    if (patch_u32(stage2_copy, stage2_padded, 0xCAFEBABEu, stage2_sectors) != 0) {
        /* best effort; stage2 manifest calculo pode falhar se nao aplicar */
    }

    /* Write stage2 */
    if (write_sector_aligned(disk, stage2_lba, stage2_copy, stage2_padded) != 0) {
        kfree(stage2_copy);
        return -1;
    }
    kfree(stage2_copy);

    /* Write manifest (1 sector) */
    uint8_t man_buf[512];
    for (size_t i = 0; i < 512; ++i) man_buf[i] = 0;
    const uint8_t *mbytes = (const uint8_t *)&manifest;
    size_t mlen = sizeof(manifest);
    if (mlen > sizeof(man_buf)) mlen = sizeof(man_buf);
    for (size_t i = 0; i < mlen; ++i) {
        man_buf[i] = mbytes[i];
    }
    if (block_device_write(disk, manifest_lba, man_buf) != 0) {
        return -1;
    }

    /* Write kernels */
    if (write_sector_aligned(disk, kernel_main_lba, payloads->kernel_main.data, payloads->kernel_main.size) != 0) {
        return -1;
    }
    if (write_sector_aligned(disk, kernel_recovery_lba, payloads->kernel_recovery.data, payloads->kernel_recovery.size) != 0) {
        return -1;
    }

    /* Finally stage1 */
    return bootwriter_write_stage1(disk, boot_part, stage2_lba, stage2_sectors, &payloads->stage1);
}
