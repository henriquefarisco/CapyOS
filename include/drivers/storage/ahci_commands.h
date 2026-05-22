/* AHCI command builders — protocol-level encoders, pure (no MMIO).
 *
 * Etapa 3 — Slice 3E.1: extract FIS / command header / PRDT encoders
 * out of `src/drivers/storage/ahci.c` so the protocol layout can be
 * host-tested without driving real MMIO. The runtime driver
 * (`ahci.c`) calls these builders and then performs the MMIO writes.
 *
 * References:
 *  - SATA AHCI Specification rev 1.3.1, §4 (FIS layouts) and §4.2.3
 *    (Command Header layout).
 *  - ATA/ATAPI Command Set ACS-3, §7 (READ DMA EXT, WRITE DMA EXT).
 *
 * Layout invariants verified by host tests in
 * `tests/drivers/test_ahci_commands.c`.
 */
#ifndef DRIVERS_STORAGE_AHCI_COMMANDS_H
#define DRIVERS_STORAGE_AHCI_COMMANDS_H

#include <stdint.h>

/* H2D Register FIS — first byte type code per AHCI §4.4.1.4. */
#define AHCI_FIS_TYPE_REG_H2D 0x27u
/* FIS length in dwords for the H2D Register FIS (5 dwords = 20 bytes
 * of the 64-byte CFIS area are meaningful). */
#define AHCI_H2D_FIS_LEN_DW 5u
/* Command Header flags layout (AHCI §4.2.3 PxCLB[i].DW0). */
#define AHCI_CMD_HEADER_FLAG_WRITE (1u << 6)
/* PRDT entry interrupt-on-complete bit (AHCI §4.2.3.3). */
#define AHCI_PRDT_FLAG_INTERRUPT (1u << 31)
/* Maximum byte count per PRDT entry (4 MiB - 1, encoded as count-1
 * in the low 22 bits of dbc_i). */
#define AHCI_PRDT_MAX_BYTES 0x00400000u

/* AHCI Command Header (entry in the per-port Command List). 32 bytes
 * each, 32 entries per port. */
struct ahci_cmd_header {
  uint16_t flags;
  uint16_t prdtl;
  uint32_t prdbc;
  uint32_t ctba;
  uint32_t ctbau;
  uint32_t reserved[4];
} __attribute__((packed));

/* AHCI PRDT entry — Physical Region Descriptor Table entry. 16 bytes
 * each. */
struct ahci_prdt_entry {
  uint32_t dba;
  uint32_t dbau;
  uint32_t reserved0;
  uint32_t dbc_i; /* low 22 bits: byte count minus 1; bit 31: I-bit */
} __attribute__((packed));

/* AHCI Command Table — referenced by the Command Header via CTBA.
 * Layout per AHCI §4.2.3.1: 64-byte CFIS region, 16-byte ATAPI
 * command region, 48 reserved bytes, then a variable-length array of
 * PRDT entries. The runtime driver currently uses a single PRDT
 * entry; multi-entry support is a Slice 3E.3 follow-up. */
struct ahci_cmd_table {
  uint8_t cfis[64];
  uint8_t acmd[16];
  uint8_t reserved[48];
  struct ahci_prdt_entry prdt[1];
} __attribute__((packed));

/* Build an H2D Register FIS for an LBA48 ATA command. `cfis` MUST
 * point to a 64-byte aligned buffer; the function zeroes the full
 * 64-byte CFIS area and then writes the meaningful fields.
 *
 * Encoding follows AHCI §4.4.1.4 / ATA-ACS §7:
 *  - cfis[0] = FIS type 0x27.
 *  - cfis[1] = 0x80 (bit 7 set: this is a command, not a control
 *    update). Lower bits (PMPort) left zero — we drive root device.
 *  - cfis[2] = command opcode (READ DMA EXT = 0x25, WRITE DMA EXT =
 *    0x35, IDENTIFY DEVICE = 0xEC, etc.).
 *  - cfis[4..6] = LBA bits 0..23.
 *  - cfis[7] = 0x40 (LBA mode, device 0).
 *  - cfis[8..10] = LBA bits 24..47.
 *  - cfis[12..13] = sector count (count, low/high).
 *
 * Returns 0 on success or -1 if `cfis` is NULL. */
int ahci_build_h2d_fis(uint8_t *cfis, uint8_t command, uint64_t lba,
                       uint16_t sector_count);

/* Populate a Command Header for the per-port Command List.
 * `header` MUST point to a 32-byte command header slot. `ctba` is the
 * physical address of the associated Command Table; the low 32 bits
 * go in `ctba` and the high 32 bits in `ctbau`. `fis_len_dw` is the
 * length of the CFIS in dwords (use AHCI_H2D_FIS_LEN_DW for the
 * standard H2D Register FIS). `prdt_len` is the number of PRDT
 * entries. `write` is nonzero for host-to-device transfers.
 *
 * Returns 0 on success or -1 if `header` is NULL or `fis_len_dw`
 * exceeds the 5-bit field (max 31). */
int ahci_build_command_header(struct ahci_cmd_header *header, uint64_t ctba,
                              uint16_t fis_len_dw, uint16_t prdt_len,
                              int write);

/* Populate a PRDT entry pointing at a contiguous data buffer.
 * `entry` MUST point to a 16-byte PRDT entry. `buffer` is the
 * physical address of the buffer; the low 32 bits go in `dba` and the
 * high 32 bits in `dbau`. `byte_count` is the number of bytes to
 * transfer; per AHCI §4.2.3.3 it MUST be even and at most
 * `AHCI_PRDT_MAX_BYTES`. If `interrupt_on_complete` is nonzero the
 * I-bit (bit 31 of dbc_i) is set.
 *
 * Returns 0 on success or -1 on invalid input (NULL entry,
 * byte_count zero, odd byte_count or above max). */
int ahci_build_prdt_entry(struct ahci_prdt_entry *entry, uint64_t buffer,
                          uint32_t byte_count, int interrupt_on_complete);

#endif /* DRIVERS_STORAGE_AHCI_COMMANDS_H */
