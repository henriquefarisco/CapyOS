#ifndef RAMDISK_H
#define RAMDISK_H

#include <stddef.h>
#include <stdint.h>

#include "fs/block.h"

#define RAMDISK_BLOCK_SIZE 4096
#define RAMDISK_MAX_BLOCKS 1024

void ramdisk_init(uint32_t block_count);
struct block_device *ramdisk_device(void);
void ramdisk_fill(const void *data, size_t size);

#endif
