#ifndef DRIVERS_STORAGE_ATA_STATUS_H
#define DRIVERS_STORAGE_ATA_STATUS_H

#include <stdint.h>

#include "drivers/storage/block_error.h"

/*
 * ATA Status register (port io_base+7) bit definitions and pure,
 * host-testable predicates. Single source of truth shared by the live
 * PIO driver (src/drivers/storage/ata_pio.c) and the host tests
 * (tests/drivers/test_ata_status.c).
 *
 * Regressive hardening (2026-05-29): ata_wait_ready() historically
 * returned success as soon as BSY cleared, without inspecting Device
 * Fault (DF) or ERR. A drive that faulted while clearing BSY — e.g.
 * during the post-write "wait for the device to finish" step in
 * ata_pio_write_sector_ctx — was therefore reported as a successful
 * command, turning a hardware fault into a SILENT read/write failure
 * (data-integrity hazard). This is the same bug class as the NVMe
 * CSTS.CFS (nvme_reset_csts_fatal) and xHCI USBSTS.HSE early-exit
 * fixes. Extracting the fatal-bit test as a pure predicate lets host
 * tests lock the contract; the live driver calls it once BSY clears
 * and bails fail-closed. Bailing early on a fault also stops the wait
 * loop from burning its full ATA_POLL_MAX budget on wedged hardware.
 *
 * Per ATA/ATAPI, ERR/DF are only meaningful once BSY == 0, so the
 * caller must clear BSY before consulting ata_status_is_fatal().
 */
#define ATA_STATUS_BSY  0x80u /* Busy */
#define ATA_STATUS_DRDY 0x40u /* Device ready */
#define ATA_STATUS_DF   0x20u /* Device fault */
#define ATA_STATUS_DSC  0x10u /* Device seek complete */
#define ATA_STATUS_DRQ  0x08u /* Data request (ready to transfer) */
#define ATA_STATUS_CORR 0x04u /* Corrected data */
#define ATA_STATUS_IDX  0x02u /* Index */
#define ATA_STATUS_ERR  0x01u /* Error */

/* Nonzero when the device reports a hard failure (Device Fault or
 * ERR). Consult only after BSY has cleared. Pure: no MMIO. */
int ata_status_is_fatal(uint8_t status);

/* Nonzero when BSY is set (command still in progress). Pure. */
int ata_status_busy(uint8_t status);

/* Nonzero when DRQ is set (device ready to transfer a data word). Pure. */
int ata_status_drq_ready(uint8_t status);

enum ata_flush_command {
  ATA_FLUSH_NONE = 0,
  ATA_FLUSH_CACHE = 0xE7,
  ATA_FLUSH_CACHE_EXT = 0xEA,
};

enum ata_flush_command ata_identify_flush_command(const uint16_t identify[256]);
enum block_io_error_class ata_flush_poll_class(uint8_t status, int timed_out);
int ata_identify_supports_lba48_dma(const uint16_t identify[256]);

#endif /* DRIVERS_STORAGE_ATA_STATUS_H */
