/* Host tests for AHCI command builders (Slice 3E.1).
 *
 * These tests exercise the pure builders in
 * src/drivers/storage/ahci_commands.c without touching MMIO or
 * kernel state, so they run under the standard host runner.
 */

#include "drivers/storage/ahci_commands.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_failures = 0;

static void fail(const char *msg) {
    printf("[ahci-commands] FAIL: %s\n", msg);
    g_failures++;
}

static void test_h2d_fis_rejects_null(void) {
    if (ahci_build_h2d_fis(NULL, 0x25u, 0, 1) != -1) {
        fail("build_h2d_fis must reject NULL cfis");
    }
}

static void test_h2d_fis_basic_read(void) {
    uint8_t cfis[64];
    /* Pre-fill with sentinel to verify the builder zeroes the buffer. */
    memset(cfis, 0xAA, sizeof(cfis));
    if (ahci_build_h2d_fis(cfis, 0x25u, 0, 1u) != 0) {
        fail("build_h2d_fis must succeed on valid input");
        return;
    }
    if (cfis[0] != AHCI_FIS_TYPE_REG_H2D) fail("byte 0 must be FIS type 0x27");
    if (cfis[1] != 0x80u) fail("byte 1 must be 0x80 (command bit)");
    if (cfis[2] != 0x25u) fail("byte 2 must be the command opcode");
    if (cfis[7] != 0x40u) fail("byte 7 must be 0x40 (LBA mode)");
    /* LBA = 0 → bytes 4..6 and 8..10 should all be zero. */
    for (int i = 4; i <= 6; i++) {
        if (cfis[i] != 0) fail("low LBA bytes must be 0 for LBA=0");
    }
    for (int i = 8; i <= 10; i++) {
        if (cfis[i] != 0) fail("high LBA bytes must be 0 for LBA=0");
    }
    if (cfis[12] != 1u || cfis[13] != 0u) {
        fail("sector count low/high must encode 1");
    }
    /* Reserved bytes (3, 11, 14..63) must be zero. */
    if (cfis[3] != 0 || cfis[11] != 0) fail("reserved bytes 3/11 must be zero");
    for (int i = 14; i < 64; i++) {
        if (cfis[i] != 0) fail("reserved trailing bytes must be zero");
    }
}

static void test_h2d_fis_lba48_high_bits(void) {
    uint8_t cfis[64];
    /* Encode a 48-bit LBA that exercises all bytes. */
    uint64_t lba = 0x0000123456789ABCull;
    if (ahci_build_h2d_fis(cfis, 0x35u, lba, 0x0102u) != 0) {
        fail("build_h2d_fis must succeed on LBA48 input");
        return;
    }
    if (cfis[2] != 0x35u) fail("opcode must be 0x35 (WRITE DMA EXT)");
    if (cfis[4] != 0xBCu) fail("LBA[7:0] mismatch");
    if (cfis[5] != 0x9Au) fail("LBA[15:8] mismatch");
    if (cfis[6] != 0x78u) fail("LBA[23:16] mismatch");
    if (cfis[8] != 0x56u) fail("LBA[31:24] mismatch");
    if (cfis[9] != 0x34u) fail("LBA[39:32] mismatch");
    if (cfis[10] != 0x12u) fail("LBA[47:40] mismatch");
    if (cfis[12] != 0x02u) fail("sector count low byte mismatch");
    if (cfis[13] != 0x01u) fail("sector count high byte mismatch");
}

static void test_h2d_fis_truncates_lba_to_48_bits(void) {
    uint8_t cfis[64];
    /* Bits above 47 must be discarded silently (LBA48). */
    uint64_t lba = 0xFFFFFFFFFFFFFFFFull;
    if (ahci_build_h2d_fis(cfis, 0xECu, lba, 0) != 0) {
        fail("build_h2d_fis must succeed on LBA48 mask");
        return;
    }
    /* All 6 LBA bytes should be 0xFF. */
    if (cfis[4] != 0xFFu || cfis[5] != 0xFFu || cfis[6] != 0xFFu ||
        cfis[8] != 0xFFu || cfis[9] != 0xFFu || cfis[10] != 0xFFu) {
        fail("LBA48 bytes must all be 0xFF when bits 0..47 are set");
    }
}

static void test_command_header_rejects_invalid(void) {
    struct ahci_cmd_header hdr;
    if (ahci_build_command_header(NULL, 0, AHCI_H2D_FIS_LEN_DW, 1, 0) != -1) {
        fail("build_command_header must reject NULL header");
    }
    if (ahci_build_command_header(&hdr, 0, 0x20u, 1, 0) != -1) {
        fail("build_command_header must reject fis_len_dw > 31");
    }
}

static void test_command_header_read_path(void) {
    struct ahci_cmd_header hdr;
    memset(&hdr, 0xAA, sizeof(hdr));
    if (ahci_build_command_header(&hdr, 0xDEADBEEFCAFEBABEull,
                                  AHCI_H2D_FIS_LEN_DW, 1u, 0) != 0) {
        fail("build_command_header must succeed on read path");
        return;
    }
    if (hdr.flags != AHCI_H2D_FIS_LEN_DW) {
        fail("flags must equal fis_len_dw when not writing");
    }
    if (hdr.prdtl != 1u) fail("prdtl must equal caller-provided value");
    if (hdr.prdbc != 0u) fail("prdbc must start at 0");
    if (hdr.ctba != 0xCAFEBABEu) fail("ctba must hold low 32 bits of CTBA");
    if (hdr.ctbau != 0xDEADBEEFu) fail("ctbau must hold high 32 bits of CTBA");
    for (int i = 0; i < 4; i++) {
        if (hdr.reserved[i] != 0u) fail("reserved words must be zeroed");
    }
}

static void test_command_header_write_flag(void) {
    struct ahci_cmd_header hdr;
    if (ahci_build_command_header(&hdr, 0, AHCI_H2D_FIS_LEN_DW, 1u, 1) != 0) {
        fail("build_command_header must succeed on write path");
        return;
    }
    if (!(hdr.flags & AHCI_CMD_HEADER_FLAG_WRITE)) {
        fail("write flag must set bit 6 of flags");
    }
    if ((hdr.flags & 0x1Fu) != AHCI_H2D_FIS_LEN_DW) {
        fail("FIS length must be preserved alongside write flag");
    }
}

static void test_prdt_entry_rejects_invalid(void) {
    struct ahci_prdt_entry e;
    if (ahci_build_prdt_entry(NULL, 0, 512u, 0) != -1) {
        fail("build_prdt_entry must reject NULL entry");
    }
    if (ahci_build_prdt_entry(&e, 0, 0u, 0) != -1) {
        fail("build_prdt_entry must reject byte_count == 0");
    }
    if (ahci_build_prdt_entry(&e, 0, 513u, 0) != -1) {
        fail("build_prdt_entry must reject odd byte_count");
    }
    if (ahci_build_prdt_entry(&e, 0, AHCI_PRDT_MAX_BYTES + 2u, 0) != -1) {
        fail("build_prdt_entry must reject byte_count above max");
    }
}

static void test_prdt_entry_encodes_byte_count_minus_one(void) {
    struct ahci_prdt_entry e;
    memset(&e, 0xAA, sizeof(e));
    if (ahci_build_prdt_entry(&e, 0x123456789ABCDEF0ull, 512u, 1) != 0) {
        fail("build_prdt_entry must succeed on valid input");
        return;
    }
    if (e.dba != 0x9ABCDEF0u) fail("dba must hold low 32 bits of buffer");
    if (e.dbau != 0x12345678u) fail("dbau must hold high 32 bits of buffer");
    if (e.reserved0 != 0u) fail("reserved0 must be zero");
    /* byte_count = 512 → dbc_i low 22 bits = 511 = 0x1FF; I-bit set. */
    if ((e.dbc_i & 0x003FFFFFu) != 511u) {
        fail("dbc_i low 22 bits must encode byte_count - 1");
    }
    if (!(e.dbc_i & AHCI_PRDT_FLAG_INTERRUPT)) {
        fail("I-bit must be set when interrupt_on_complete=1");
    }
    /* Bits 22..30 reserved must be zero. */
    if (e.dbc_i & 0x7FC00000u) {
        fail("dbc_i reserved bits 22..30 must be zero");
    }
}

static void test_prdt_entry_no_interrupt_bit(void) {
    struct ahci_prdt_entry e;
    if (ahci_build_prdt_entry(&e, 0, 1024u, 0) != 0) {
        fail("build_prdt_entry must succeed without interrupt");
        return;
    }
    if (e.dbc_i & AHCI_PRDT_FLAG_INTERRUPT) {
        fail("I-bit must NOT be set when interrupt_on_complete=0");
    }
    if ((e.dbc_i & 0x003FFFFFu) != 1023u) {
        fail("byte_count - 1 must be encoded correctly without I-bit");
    }
}

int run_ahci_commands_tests(void) {
    g_failures = 0;
    test_h2d_fis_rejects_null();
    test_h2d_fis_basic_read();
    test_h2d_fis_lba48_high_bits();
    test_h2d_fis_truncates_lba_to_48_bits();
    test_command_header_rejects_invalid();
    test_command_header_read_path();
    test_command_header_write_flag();
    test_prdt_entry_rejects_invalid();
    test_prdt_entry_encodes_byte_count_minus_one();
    test_prdt_entry_no_interrupt_bit();
    if (g_failures == 0) printf("[tests] ahci_commands OK\n");
    return g_failures;
}
