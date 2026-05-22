/* Unified block-I/O error classification for AHCI / NVMe / ATA-PIO.
 *
 * Etapa 3 — Slice 3E.2: every storage driver classifies its
 * completion / timeout outcome into a small enum that the upper
 * layer can use to decide whether to retry, surface the error or
 * mark the device as gone. The classifier itself is pure logic
 * (host-testable, no MMIO).
 *
 * The retry policy itself (3 attempts for TRANSIENT, 0 for
 * PERMANENT, 1 with COMRESET / Abort for TIMEOUT, 0 for
 * DEVICE_GONE) is implemented by the caller; this header only
 * defines the vocabulary.
 *
 * References:
 *  - SATA AHCI 1.3.1 §5.4 (Port Task File) — TFD layout.
 *  - ATA/ATAPI-ACS3 §7.16 (ERROR register codes).
 *  - NVMe 1.4c §4.6.1 (Completion Queue Entry — Status Field).
 */
#ifndef DRIVERS_STORAGE_BLOCK_ERROR_H
#define DRIVERS_STORAGE_BLOCK_ERROR_H

#include <stdint.h>

enum block_io_error_class {
  /* Operation succeeded. */
  BLOCK_IO_OK = 0,
  /* Recoverable error — caller should retry up to N times. Examples:
   *  - AHCI ICRC (interface CRC mismatch).
   *  - NVMe SC with DNR=0 and SCT != Media/Path. */
  BLOCK_IO_ERR_TRANSIENT = 1,
  /* Permanent error — caller should surface to the user / fs layer.
   * Examples:
   *  - AHCI UNC (uncorrectable data).
   *  - NVMe SC with DNR=1, or media integrity / write-fault. */
  BLOCK_IO_ERR_PERMANENT = 2,
  /* Driver timed out waiting for completion. Caller should issue a
   * reset (AHCI: COMRESET on the port; NVMe: Abort command, fall
   * through to Controller Level Reset on second failure) and retry
   * exactly once. */
  BLOCK_IO_ERR_TIMEOUT = 3,
  /* Device disappeared. Caller should mark the block_device as
   * removed and stop issuing I/O. Examples:
   *  - AHCI port lost link (SSTS.DET != PRESENT).
   *  - NVMe SCT=3 (Path Related Status). */
  BLOCK_IO_ERR_DEVICE_GONE = 4,
};

/* Classify an AHCI command completion outcome.
 *
 * Inputs:
 *  - `pxis`: PxIS register snapshot AFTER the command completed or
 *    after the driver gave up spinning.
 *  - `pxtfd`: PxTFD register snapshot — bits [7:0] STATUS, bits
 *    [15:8] ERROR. Only consulted when IS.TFES or TFD.STS.ERR is
 *    set.
 *  - `timed_out`: nonzero if the driver bailed out via its spin
 *    counter without ever seeing the slot clear in PxCI.
 *  - `port_present`: 0 if SSTS.DET no longer reports PRESENT (link
 *    lost during the operation).
 *
 * Decision order:
 *  1. !port_present → DEVICE_GONE (highest priority — the device is
 *     gone, no point retrying).
 *  2. timed_out → TIMEOUT.
 *  3. No TFES and no TFD.STS.ERR → OK.
 *  4. TFD.ERR.UNC (0x40) or TFD.ERR.IDNF (0x10) → PERMANENT
 *     (uncorrectable data or address invalid — retry will not help).
 *  5. TFD.ERR.ABRT (0x04) → PERMANENT (controller refused).
 *  6. TFD.ERR.ICRC (0x80) or no ERROR bits set with TFES → TRANSIENT.
 *  7. Default (other ERROR bits, e.g. MC, NM) → TRANSIENT. */
enum block_io_error_class block_io_classify_ahci(uint32_t pxis, uint32_t pxtfd,
                                                 int timed_out,
                                                 int port_present);

/* Classify an NVMe completion outcome.
 *
 * Inputs:
 *  - `status`: 16-bit Status Field from the CQE. Layout (NVMe 1.4
 *    §4.6.1.2):
 *      bit 0      P    (phase, ignored here)
 *      bits 1-8   SC   (Status Code)
 *      bits 9-11  SCT  (Status Code Type)
 *      bits 12-13 CRD  (Command Retry Delay)
 *      bit 14     M    (More)
 *      bit 15     DNR  (Do Not Retry)
 *  - `timed_out`: nonzero if the driver bailed out spinning for the
 *    phase flip without ever observing a completion.
 *
 * Decision order:
 *  1. timed_out → TIMEOUT.
 *  2. SC == 0 (Successful Completion) → OK.
 *  3. SCT == 3 (Path Related Status) → DEVICE_GONE.
 *  4. SCT == 2 (Media and Data Integrity Errors) → PERMANENT.
 *  5. DNR == 1 → PERMANENT (controller said don't retry).
 *  6. Otherwise → TRANSIENT (default retryable). */
enum block_io_error_class block_io_classify_nvme(uint16_t status,
                                                 int timed_out);

/* Stable lowercase short name suitable for klog. Never NULL. */
const char *block_io_error_class_name(enum block_io_error_class cls);

/* True when the policy recommends a retry. Convenience for callers
 * that want a one-line decision without inspecting the enum. */
int block_io_should_retry(enum block_io_error_class cls);

#endif /* DRIVERS_STORAGE_BLOCK_ERROR_H */
