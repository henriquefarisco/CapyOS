/* Host tests for the unified block-I/O classifier (Slice 3E.2). */

#include "drivers/storage/block_error.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_failures = 0;

static void fail(const char *msg) {
    printf("[block-error] FAIL: %s\n", msg);
    g_failures++;
}

/* AHCI helpers. */
#define AHCI_IS_TFES (1u << 30)
#define AHCI_TFD_ERR_BIT 0x01u
#define AHCI_TFD_MAKE(err_byte, sts) (((uint32_t)(err_byte) << 8) | (sts))

static void test_ahci_ok_clean(void) {
    enum block_io_error_class c =
        block_io_classify_ahci(0, 0, /*timed_out=*/0, /*present=*/1);
    if (c != BLOCK_IO_OK) fail("clean PxIS+PxTFD must classify as OK");
}

static void test_ahci_device_gone_wins(void) {
    /* Even with a TFES + UNC, !present must still classify as
     * DEVICE_GONE because there is no recovery possible. */
    uint32_t tfd = AHCI_TFD_MAKE(0x40u, AHCI_TFD_ERR_BIT);
    enum block_io_error_class c =
        block_io_classify_ahci(AHCI_IS_TFES, tfd, /*timed_out=*/0,
                               /*present=*/0);
    if (c != BLOCK_IO_ERR_DEVICE_GONE) {
        fail("device gone must override TFES/UNC");
    }
}

static void test_ahci_timeout(void) {
    enum block_io_error_class c =
        block_io_classify_ahci(0, 0, /*timed_out=*/1, /*present=*/1);
    if (c != BLOCK_IO_ERR_TIMEOUT) {
        fail("timed_out with port present must classify as TIMEOUT");
    }
}

static void test_ahci_unc_is_permanent(void) {
    uint32_t tfd = AHCI_TFD_MAKE(0x40u /*UNC*/, AHCI_TFD_ERR_BIT);
    enum block_io_error_class c =
        block_io_classify_ahci(AHCI_IS_TFES, tfd, 0, 1);
    if (c != BLOCK_IO_ERR_PERMANENT) fail("UNC must classify as PERMANENT");
}

static void test_ahci_abrt_is_permanent(void) {
    uint32_t tfd = AHCI_TFD_MAKE(0x04u /*ABRT*/, AHCI_TFD_ERR_BIT);
    enum block_io_error_class c =
        block_io_classify_ahci(AHCI_IS_TFES, tfd, 0, 1);
    if (c != BLOCK_IO_ERR_PERMANENT) fail("ABRT must classify as PERMANENT");
}

static void test_ahci_icrc_is_transient(void) {
    uint32_t tfd = AHCI_TFD_MAKE(0x80u /*ICRC*/, AHCI_TFD_ERR_BIT);
    enum block_io_error_class c =
        block_io_classify_ahci(AHCI_IS_TFES, tfd, 0, 1);
    if (c != BLOCK_IO_ERR_TRANSIENT) fail("ICRC must classify as TRANSIENT");
}

static void test_ahci_tfes_no_error_byte_is_transient(void) {
    /* IS.TFES set but ERROR register zero — controller signalled a
     * task-file error without identifying a specific cause; treat
     * as recoverable so the upper layer can retry. */
    uint32_t tfd = AHCI_TFD_MAKE(0x00u, AHCI_TFD_ERR_BIT);
    enum block_io_error_class c =
        block_io_classify_ahci(AHCI_IS_TFES, tfd, 0, 1);
    if (c != BLOCK_IO_ERR_TRANSIENT) {
        fail("TFES with empty ERROR byte must classify as TRANSIENT");
    }
}

static void test_ahci_idnf_is_permanent(void) {
    uint32_t tfd = AHCI_TFD_MAKE(0x10u /*IDNF*/, AHCI_TFD_ERR_BIT);
    enum block_io_error_class c =
        block_io_classify_ahci(AHCI_IS_TFES, tfd, 0, 1);
    if (c != BLOCK_IO_ERR_PERMANENT) fail("IDNF must classify as PERMANENT");
}

/* NVMe helpers — build a status word from SC + SCT (+ DNR). */
static uint16_t nvme_status(uint8_t sc, uint8_t sct, int dnr) {
    uint16_t s = (uint16_t)((sct & 0x7u) << 9) | (uint16_t)((sc) << 1);
    if (dnr) s |= (uint16_t)(1u << 15);
    return s;
}

static void test_nvme_ok(void) {
    enum block_io_error_class c = block_io_classify_nvme(nvme_status(0, 0, 0),
                                                         /*timed_out=*/0);
    if (c != BLOCK_IO_OK) fail("SC=0,SCT=0 must classify as OK");
}

static void test_nvme_timeout(void) {
    enum block_io_error_class c = block_io_classify_nvme(0, /*timed_out=*/1);
    if (c != BLOCK_IO_ERR_TIMEOUT) {
        fail("timed_out must classify as TIMEOUT even with status=0");
    }
}

static void test_nvme_path_gone(void) {
    enum block_io_error_class c = block_io_classify_nvme(
        nvme_status(/*SC=*/0x02, /*SCT=*/3, /*dnr=*/0), 0);
    if (c != BLOCK_IO_ERR_DEVICE_GONE) {
        fail("SCT=3 (Path Related Status) must classify as DEVICE_GONE");
    }
}

static void test_nvme_media_permanent(void) {
    /* SCT=2 Media — e.g. Unrecovered Read Error (SC=0x81). */
    enum block_io_error_class c = block_io_classify_nvme(
        nvme_status(/*SC=*/0x81, /*SCT=*/2, /*dnr=*/0), 0);
    if (c != BLOCK_IO_ERR_PERMANENT) {
        fail("SCT=2 (Media) must classify as PERMANENT");
    }
}

static void test_nvme_dnr_permanent(void) {
    /* Generic SC with DNR bit set. */
    enum block_io_error_class c = block_io_classify_nvme(
        nvme_status(/*SC=*/0x06, /*SCT=*/0, /*dnr=*/1), 0);
    if (c != BLOCK_IO_ERR_PERMANENT) {
        fail("DNR=1 must classify as PERMANENT regardless of SC");
    }
}

static void test_nvme_default_transient(void) {
    /* Generic SC, no DNR, no special SCT — caller may retry. */
    enum block_io_error_class c = block_io_classify_nvme(
        nvme_status(/*SC=*/0x06 /*Internal Error*/, /*SCT=*/0, 0), 0);
    if (c != BLOCK_IO_ERR_TRANSIENT) {
        fail("Generic SC without DNR must classify as TRANSIENT");
    }
}

static void test_class_name_and_retry_policy(void) {
    if (strcmp(block_io_error_class_name(BLOCK_IO_OK), "ok") != 0) {
        fail("name(OK) must be 'ok'");
    }
    if (strcmp(block_io_error_class_name(BLOCK_IO_ERR_TRANSIENT),
               "transient") != 0) {
        fail("name(TRANSIENT) must be 'transient'");
    }
    if (strcmp(block_io_error_class_name(BLOCK_IO_ERR_DEVICE_GONE),
               "device-gone") != 0) {
        fail("name(DEVICE_GONE) must be 'device-gone'");
    }
    if (block_io_should_retry(BLOCK_IO_OK)) {
        fail("should_retry(OK) must be 0");
    }
    if (!block_io_should_retry(BLOCK_IO_ERR_TRANSIENT)) {
        fail("should_retry(TRANSIENT) must be 1");
    }
    if (block_io_should_retry(BLOCK_IO_ERR_PERMANENT)) {
        fail("should_retry(PERMANENT) must be 0");
    }
    if (!block_io_should_retry(BLOCK_IO_ERR_TIMEOUT)) {
        fail("should_retry(TIMEOUT) must be 1 (after reset)");
    }
    if (block_io_should_retry(BLOCK_IO_ERR_DEVICE_GONE)) {
        fail("should_retry(DEVICE_GONE) must be 0");
    }
}

static void test_scsi_ok(void) {
    enum block_io_error_class c =
        block_io_classify_scsi(0x00u, 0x01u, 0, 1);
    if (c != BLOCK_IO_OK) fail("SCSI GOOD + SRB success must classify as OK");
}

static void test_scsi_timeout(void) {
    enum block_io_error_class c =
        block_io_classify_scsi(0x00u, 0x01u, 1, 1);
    if (c != BLOCK_IO_ERR_TIMEOUT) {
        fail("SCSI timed_out must classify as TIMEOUT");
    }
}

static void test_scsi_device_gone(void) {
    enum block_io_error_class c =
        block_io_classify_scsi(0x00u, 0x01u, 0, 0);
    if (c != BLOCK_IO_ERR_DEVICE_GONE) {
        fail("SCSI !present must classify as DEVICE_GONE");
    }
}

static void test_scsi_busy_transient(void) {
    enum block_io_error_class c =
        block_io_classify_scsi(0x08u, 0x01u, 0, 1);
    if (c != BLOCK_IO_ERR_TRANSIENT) {
        fail("SCSI BUSY must classify as TRANSIENT");
    }
}

static void test_scsi_check_condition_permanent(void) {
    enum block_io_error_class c =
        block_io_classify_scsi(0x02u, 0x01u, 0, 1);
    if (c != BLOCK_IO_ERR_PERMANENT) {
        fail("SCSI CHECK CONDITION without sense must be PERMANENT");
    }
}

static void test_scsi_srb_selection_timeout_gone(void) {
    enum block_io_error_class c =
        block_io_classify_scsi(0x00u, 0x0Au, 0, 1);
    if (c != BLOCK_IO_ERR_DEVICE_GONE) {
        fail("SRB selection timeout must classify as DEVICE_GONE");
    }
}

static void test_scsi_srb_no_device_gone(void) {
    enum block_io_error_class c =
        block_io_classify_scsi(0x00u, 0x08u, 0, 1);
    if (c != BLOCK_IO_ERR_DEVICE_GONE) {
        fail("SRB no-device must classify as DEVICE_GONE");
    }
}

static void test_scsi_srb_bus_reset_transient(void) {
    enum block_io_error_class c =
        block_io_classify_scsi(0x00u, 0x0Eu, 0, 1);
    if (c != BLOCK_IO_ERR_TRANSIENT) {
        fail("SRB bus reset must classify as TRANSIENT");
    }
}

static void test_scsi_srb_status_flags_masked(void) {
    enum block_io_error_class c =
        block_io_classify_scsi(0x00u, 0x8Eu, 0, 1);
    if (c != BLOCK_IO_ERR_TRANSIENT) {
        fail("SRB status flags must not hide the base status");
    }
}

int run_block_error_tests(void) {
    g_failures = 0;
    test_ahci_ok_clean();
    test_ahci_device_gone_wins();
    test_ahci_timeout();
    test_ahci_unc_is_permanent();
    test_ahci_abrt_is_permanent();
    test_ahci_icrc_is_transient();
    test_ahci_tfes_no_error_byte_is_transient();
    test_ahci_idnf_is_permanent();
    test_nvme_ok();
    test_nvme_timeout();
    test_nvme_path_gone();
    test_nvme_media_permanent();
    test_nvme_dnr_permanent();
    test_nvme_default_transient();
    test_scsi_ok();
    test_scsi_timeout();
    test_scsi_device_gone();
    test_scsi_busy_transient();
    test_scsi_check_condition_permanent();
    test_scsi_srb_selection_timeout_gone();
    test_scsi_srb_no_device_gone();
    test_scsi_srb_bus_reset_transient();
    test_scsi_srb_status_flags_masked();
    test_class_name_and_retry_policy();
    if (g_failures == 0) printf("[tests] block_error OK\n");
    return g_failures;
}
