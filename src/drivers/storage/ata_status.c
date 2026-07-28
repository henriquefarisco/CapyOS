#include "drivers/storage/ata_status.h"

/* Pure ATA status predicates — no MMIO, no allocations, host-testable.
 * See drivers/storage/ata_status.h for the hardening rationale. */

int ata_status_is_fatal(uint8_t status) {
  return (status & (ATA_STATUS_DF | ATA_STATUS_ERR)) != 0u;
}

int ata_status_busy(uint8_t status) {
  return (status & ATA_STATUS_BSY) != 0u;
}

int ata_status_drq_ready(uint8_t status) {
  return (status & ATA_STATUS_DRQ) != 0u;
}

enum block_io_error_class ata_flush_poll_class(uint8_t status, int timed_out) {
  if (status == 0xFFu)
    return BLOCK_IO_ERR_DEVICE_GONE;
  if (timed_out)
    return BLOCK_IO_ERR_TIMEOUT;
  if (ata_status_busy(status))
    return BLOCK_IO_ERR_TRANSIENT;
  if (ata_status_is_fatal(status) || ata_status_drq_ready(status) ||
      status == 0u)
    return BLOCK_IO_ERR_PERMANENT;
  return BLOCK_IO_OK;
}

int ata_identify_supports_lba48_dma(const uint16_t identify[256]) {
  if (!identify || (identify[83] & 0xC000u) != 0x4000u)
    return 0;
  return (identify[83] & (1u << 10)) != 0u &&
         (identify[49] & (1u << 8)) != 0u;
}

enum ata_flush_command ata_identify_flush_command(const uint16_t identify[256]) {
  uint16_t command_set;
  if (!identify)
    return ATA_FLUSH_NONE;
  command_set = identify[83];
  if ((command_set & 0xC000u) != 0x4000u)
    return ATA_FLUSH_NONE;
  if ((command_set & (1u << 13)) != 0u)
    return ATA_FLUSH_CACHE_EXT;
  if ((command_set & (1u << 12)) != 0u)
    return ATA_FLUSH_CACHE;
  return ATA_FLUSH_NONE;
}
