#ifndef DRIVERS_STORAGE_AHCI_H
#define DRIVERS_STORAGE_AHCI_H

#include "fs/block.h"

int ahci_init(void);
int ahci_device_count(void);
struct block_device *ahci_get_block_device(int index);

#endif /* DRIVERS_STORAGE_AHCI_H */
