/* Unified block-I/O error classifier (Slice 3E.2 + Hyper-V StorVSC prep).
 *
 * Pure logic — no MMIO, no kmalloc, no klog. Suitable for the host
 * runner and for the runtime drivers alike.
 */

#include "drivers/storage/block_error.h"

/* AHCI PxIS bits we care about. */
#define BLK_AHCI_IS_TFES (1u << 30)

/* AHCI PxTFD bit layout.
 * [7:0] STATUS register; bit 0 is ERR.
 * [15:8] ERROR register. */
#define BLK_AHCI_TFD_STS_ERR (1u << 0)
#define BLK_AHCI_TFD_ERROR_SHIFT 8u
#define BLK_AHCI_TFD_ERROR_MASK 0xFFu

/* ATA-ACS ERROR register bits (the ones we classify). */
#define BLK_ATA_ERR_AMNF (1u << 0)  /* Address mark not found */
#define BLK_ATA_ERR_NM (1u << 1)    /* No media */
#define BLK_ATA_ERR_ABRT (1u << 2)  /* Command aborted */
#define BLK_ATA_ERR_MCR (1u << 3)   /* Media change request */
#define BLK_ATA_ERR_IDNF (1u << 4)  /* ID not found */
#define BLK_ATA_ERR_MC (1u << 5)    /* Media changed */
#define BLK_ATA_ERR_UNC (1u << 6)   /* Uncorrectable data */
#define BLK_ATA_ERR_ICRC (1u << 7)  /* Interface CRC */

/* NVMe status field layout helpers. */
#define BLK_NVME_SC_SHIFT 1u
#define BLK_NVME_SC_MASK 0xFFu
#define BLK_NVME_SCT_SHIFT 9u
#define BLK_NVME_SCT_MASK 0x7u
#define BLK_NVME_DNR_BIT (1u << 15)

#define BLK_SCSI_STATUS_GOOD 0x00u
#define BLK_SCSI_STATUS_CHECK_CONDITION 0x02u
#define BLK_SCSI_STATUS_BUSY 0x08u
#define BLK_SCSI_STATUS_RESERVATION_CONFLICT 0x18u
#define BLK_SCSI_STATUS_TASK_SET_FULL 0x28u
#define BLK_SCSI_STATUS_ACA_ACTIVE 0x30u
#define BLK_SCSI_STATUS_TASK_ABORTED 0x40u

#define BLK_SRB_STATUS_MASK 0x3Fu
#define BLK_SRB_STATUS_SUCCESS 0x01u
#define BLK_SRB_STATUS_ABORTED 0x02u
#define BLK_SRB_STATUS_BUSY 0x05u
#define BLK_SRB_STATUS_INVALID_PATH_ID 0x07u
#define BLK_SRB_STATUS_NO_DEVICE 0x08u
#define BLK_SRB_STATUS_TIMEOUT 0x09u
#define BLK_SRB_STATUS_SELECTION_TIMEOUT 0x0Au
#define BLK_SRB_STATUS_COMMAND_TIMEOUT 0x0Bu
#define BLK_SRB_STATUS_BUS_RESET 0x0Eu
#define BLK_SRB_STATUS_PARITY_ERROR 0x0Fu
#define BLK_SRB_STATUS_NO_HBA 0x11u
#define BLK_SRB_STATUS_UNEXPECTED_BUS_FREE 0x13u
#define BLK_SRB_STATUS_PHASE_SEQUENCE_FAILURE 0x14u
#define BLK_SRB_STATUS_REQUEST_FLUSHED 0x16u
#define BLK_SRB_STATUS_INVALID_LUN 0x20u
#define BLK_SRB_STATUS_INVALID_TARGET_ID 0x21u
#define BLK_SRB_STATUS_ERROR_RECOVERY 0x23u

enum block_io_error_class block_io_classify_ahci(uint32_t pxis, uint32_t pxtfd,
                                                 int timed_out,
                                                 int port_present) {
  uint32_t err;
  /* 1. Device removed during the operation overrides everything
   * else — there is no point retrying or reporting a specific
   * error class. */
  if (!port_present) {
    return BLOCK_IO_ERR_DEVICE_GONE;
  }
  /* 2. Driver gave up spinning. Caller should reset the port. */
  if (timed_out) {
    return BLOCK_IO_ERR_TIMEOUT;
  }
  /* 3. No task-file error AND no IS.TFES → success. */
  if ((pxis & BLK_AHCI_IS_TFES) == 0u &&
      (pxtfd & BLK_AHCI_TFD_STS_ERR) == 0u) {
    return BLOCK_IO_OK;
  }
  err = (pxtfd >> BLK_AHCI_TFD_ERROR_SHIFT) & BLK_AHCI_TFD_ERROR_MASK;
  /* 4. Hard data errors — retrying will not change the outcome. */
  if (err & (BLK_ATA_ERR_UNC | BLK_ATA_ERR_IDNF | BLK_ATA_ERR_AMNF)) {
    return BLOCK_IO_ERR_PERMANENT;
  }
  if (err & BLK_ATA_ERR_ABRT) {
    /* Controller-side refusal. May come and go but in practice
     * indicates the command is malformed for this device; treat as
     * permanent so the caller surfaces it instead of looping. */
    return BLOCK_IO_ERR_PERMANENT;
  }
  /* 5. Everything else — ICRC, NM, MCR, MC, generic TFES with no
   * ERROR bits — is recoverable. */
  return BLOCK_IO_ERR_TRANSIENT;
}

enum block_io_error_class block_io_classify_nvme(uint16_t status,
                                                 int timed_out) {
  uint16_t sc;
  uint16_t sct;
  if (timed_out) {
    return BLOCK_IO_ERR_TIMEOUT;
  }
  sc = (status >> BLK_NVME_SC_SHIFT) & BLK_NVME_SC_MASK;
  sct = (status >> BLK_NVME_SCT_SHIFT) & BLK_NVME_SCT_MASK;
  /* Successful Completion (Generic SC=0x00) — note SCT must also
   * be 0 for this to be a clean success. */
  if (sc == 0u && sct == 0u) {
    return BLOCK_IO_OK;
  }
  /* Path Related Status — the device or fabric is gone. */
  if (sct == 3u) {
    return BLOCK_IO_ERR_DEVICE_GONE;
  }
  /* Media and Data Integrity Errors — caller will not recover by
   * retrying the same LBA. */
  if (sct == 2u) {
    return BLOCK_IO_ERR_PERMANENT;
  }
  /* Do-Not-Retry — controller explicitly says retry is futile. */
  if (status & BLK_NVME_DNR_BIT) {
    return BLOCK_IO_ERR_PERMANENT;
  }
  /* Default — Generic / Command Specific without DNR is treated as
   * recoverable; the caller's retry budget bounds the loop. */
  return BLOCK_IO_ERR_TRANSIENT;
}

enum block_io_error_class block_io_classify_scsi(uint8_t scsi_status,
                                                 uint8_t srb_status,
                                                 int timed_out,
                                                 int device_present) {
  uint8_t srb_base;
  if (!device_present) {
    return BLOCK_IO_ERR_DEVICE_GONE;
  }
  if (timed_out) {
    return BLOCK_IO_ERR_TIMEOUT;
  }
  srb_base = srb_status & BLK_SRB_STATUS_MASK;
  switch (srb_base) {
  case 0u:
  case BLK_SRB_STATUS_SUCCESS:
    break;
  case BLK_SRB_STATUS_TIMEOUT:
  case BLK_SRB_STATUS_COMMAND_TIMEOUT:
    return BLOCK_IO_ERR_TIMEOUT;
  case BLK_SRB_STATUS_SELECTION_TIMEOUT:
  case BLK_SRB_STATUS_INVALID_PATH_ID:
  case BLK_SRB_STATUS_NO_DEVICE:
  case BLK_SRB_STATUS_NO_HBA:
  case BLK_SRB_STATUS_INVALID_LUN:
  case BLK_SRB_STATUS_INVALID_TARGET_ID:
    return BLOCK_IO_ERR_DEVICE_GONE;
  case BLK_SRB_STATUS_ABORTED:
  case BLK_SRB_STATUS_BUSY:
  case BLK_SRB_STATUS_BUS_RESET:
  case BLK_SRB_STATUS_PARITY_ERROR:
  case BLK_SRB_STATUS_UNEXPECTED_BUS_FREE:
  case BLK_SRB_STATUS_PHASE_SEQUENCE_FAILURE:
  case BLK_SRB_STATUS_REQUEST_FLUSHED:
  case BLK_SRB_STATUS_ERROR_RECOVERY:
    return BLOCK_IO_ERR_TRANSIENT;
  default:
    return BLOCK_IO_ERR_PERMANENT;
  }
  switch (scsi_status) {
  case BLK_SCSI_STATUS_GOOD:
    return BLOCK_IO_OK;
  case BLK_SCSI_STATUS_BUSY:
  case BLK_SCSI_STATUS_TASK_SET_FULL:
  case BLK_SCSI_STATUS_ACA_ACTIVE:
  case BLK_SCSI_STATUS_TASK_ABORTED:
    return BLOCK_IO_ERR_TRANSIENT;
  case BLK_SCSI_STATUS_CHECK_CONDITION:
  case BLK_SCSI_STATUS_RESERVATION_CONFLICT:
  default:
    return BLOCK_IO_ERR_PERMANENT;
  }
}

const char *block_io_error_class_name(enum block_io_error_class cls) {
  switch (cls) {
    case BLOCK_IO_OK:
      return "ok";
    case BLOCK_IO_ERR_TRANSIENT:
      return "transient";
    case BLOCK_IO_ERR_PERMANENT:
      return "permanent";
    case BLOCK_IO_ERR_TIMEOUT:
      return "timeout";
    case BLOCK_IO_ERR_DEVICE_GONE:
      return "device-gone";
  }
  return "unknown";
}

int block_io_should_retry(enum block_io_error_class cls) {
  /* TIMEOUT is special: the policy says retry exactly once AFTER a
   * reset, not a plain retry. We still answer "yes" here so the
   * caller knows the slot is reusable; the caller is responsible
   * for issuing the reset and bounding to one retry. */
  return cls == BLOCK_IO_ERR_TRANSIENT || cls == BLOCK_IO_ERR_TIMEOUT;
}
