/* AHCI command builders — pure protocol encoders.
 *
 * Etapa 3 — Slice 3E.1: extracted from `src/drivers/storage/ahci.c`
 * to allow host testing without driving real MMIO. The runtime
 * driver calls these builders, then performs the MMIO writes that
 * commit the command to the controller.
 *
 * The functions in this TU MUST NOT touch MMIO, kmalloc, klog or
 * any host-only API. They only write into caller-provided memory.
 */

#include "drivers/storage/ahci_commands.h"

#include <stddef.h>

static void ahci_zero(void *dst, uint32_t len) {
  uint8_t *p = (uint8_t *)dst;
  uint32_t i;
  for (i = 0u; i < len; ++i) {
    p[i] = 0;
  }
}

int ahci_build_h2d_fis(uint8_t *cfis, uint8_t command, uint64_t lba,
                       uint16_t sector_count) {
  if (cfis == NULL) {
    return -1;
  }
  ahci_zero(cfis, 64u);
  cfis[0] = AHCI_FIS_TYPE_REG_H2D;
  /* Bit 7: command register update (this FIS conveys a new command,
   * not a control bit change). Bits 0..3: Port Multiplier port —
   * left at 0 for the root device. */
  cfis[1] = 1u << 7;
  cfis[2] = command;
  /* LBA[23:0] */
  cfis[4] = (uint8_t)(lba & 0xFFu);
  cfis[5] = (uint8_t)((lba >> 8) & 0xFFu);
  cfis[6] = (uint8_t)((lba >> 16) & 0xFFu);
  /* Device register: LBA mode, device 0. Bit 6 = LBA, bit 4 = DEV. */
  cfis[7] = 1u << 6;
  /* LBA[47:24] */
  cfis[8] = (uint8_t)((lba >> 24) & 0xFFu);
  cfis[9] = (uint8_t)((lba >> 32) & 0xFFu);
  cfis[10] = (uint8_t)((lba >> 40) & 0xFFu);
  /* Sector count (LBA48 uses 16 bits). */
  cfis[12] = (uint8_t)(sector_count & 0xFFu);
  cfis[13] = (uint8_t)((sector_count >> 8) & 0xFFu);
  return 0;
}

int ahci_build_command_header(struct ahci_cmd_header *header, uint64_t ctba,
                              uint16_t fis_len_dw, uint16_t prdt_len,
                              int write) {
  uint32_t i;
  if (header == NULL) {
    return -1;
  }
  /* PxCLB[i].DW0[4:0] holds the FIS length in dwords (5 bits). */
  if (fis_len_dw > 0x1Fu) {
    return -1;
  }
  header->flags = (uint16_t)(fis_len_dw & 0x1Fu);
  if (write) {
    header->flags |= (uint16_t)AHCI_CMD_HEADER_FLAG_WRITE;
  }
  header->prdtl = prdt_len;
  header->prdbc = 0u;
  header->ctba = (uint32_t)(ctba & 0xFFFFFFFFu);
  header->ctbau = (uint32_t)((ctba >> 32) & 0xFFFFFFFFu);
  for (i = 0u; i < 4u; ++i) {
    header->reserved[i] = 0u;
  }
  return 0;
}

int ahci_build_prdt_entry(struct ahci_prdt_entry *entry, uint64_t buffer,
                          uint32_t byte_count, int interrupt_on_complete) {
  if (entry == NULL) {
    return -1;
  }
  if (byte_count == 0u || byte_count > AHCI_PRDT_MAX_BYTES) {
    return -1;
  }
  /* AHCI §4.2.3.3: the byte count MUST be even because the AHCI
   * controller transfers 16-bit words; odd counts produce undefined
   * behaviour. Catch the misuse here rather than at runtime. */
  if ((byte_count & 1u) != 0u) {
    return -1;
  }
  entry->dba = (uint32_t)(buffer & 0xFFFFFFFFu);
  entry->dbau = (uint32_t)((buffer >> 32) & 0xFFFFFFFFu);
  entry->reserved0 = 0u;
  /* dbc_i: low 22 bits hold (byte_count - 1); high bit (31) is the
   * Interrupt-on-Completion flag. Bits 22..30 are reserved and MUST
   * be zero. */
  entry->dbc_i = (byte_count - 1u) & 0x003FFFFFu;
  if (interrupt_on_complete) {
    entry->dbc_i |= AHCI_PRDT_FLAG_INTERRUPT;
  }
  return 0;
}
