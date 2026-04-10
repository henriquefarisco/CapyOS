#ifndef STORAGE_PARTITION_H
#define STORAGE_PARTITION_H

#include <stdint.h>

#include "fs/block.h"

struct mbr_partition {
    uint32_t lba_start;
    uint32_t sector_count;
    uint8_t type;
    uint8_t bootable;
};

// Le a entrada N (0-3) da tabela de particoes do MBR do dispositivo RAW (512B).
// Retorna 0 em sucesso e preenche out; -1 em qualquer erro (dispositivo sem MBR valido ou index invalido).
int mbr_read_partition(struct block_device *raw, int index, struct mbr_partition *out);

#endif
