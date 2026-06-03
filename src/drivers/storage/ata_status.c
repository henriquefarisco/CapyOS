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
