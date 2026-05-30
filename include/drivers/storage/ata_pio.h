#ifndef DRIVERS_STORAGE_ATA_PIO_H
#define DRIVERS_STORAGE_ATA_PIO_H

#include "fs/block.h"

/*
 * Minimal ATA-PIO driver public interface.
 *
 * Used as a broad-hardware-compatibility fallback for hypervisor
 * environments that expose legacy IDE/ATA emulation instead of, or in
 * addition to, NVMe / AHCI / VMBus-synthetic backends:
 *
 *   - Hyper-V Generation 1 (IDE/ATA emulation on 0x1F0/0x170 channels);
 *   - older QEMU/Bochs/VirtualBox machines exposing the legacy IDE bus;
 *   - bare metal hosts where the firmware exposes ATA legacy fallback.
 *
 * The VMware + UEFI + E1000 official validation track uses NVMe/AHCI;
 * this driver is intentionally additive and never replaces those paths.
 */

void ata_init(void);
int ata_devices_count(void);
struct block_device *ata_device_by_index(int idx);
struct block_device *ata_primary_device(void);

#endif /* DRIVERS_STORAGE_ATA_PIO_H */
