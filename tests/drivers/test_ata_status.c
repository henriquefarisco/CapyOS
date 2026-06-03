/* Host tests for the ATA status pure predicates (regressive
 * hardening 2026-05-29). Exercises src/drivers/storage/ata_status.c —
 * no MMIO, no kernel state, runnable under the standard host runner.
 *
 * Locks the fatal-status fix: ata_wait_ready()/ata_wait_drq() must
 * treat Device Fault (DF) or ERR as a hard failure once BSY clears,
 * instead of silently reporting success. Same bug class as the NVMe
 * CSTS.CFS (nvme_reset_csts_fatal) and xHCI USBSTS.HSE early-exit
 * fixes; this pins the predicate so a regression that drops the DF/ERR
 * check shows up on the host before any VMware storage smoke. */

#include "drivers/storage/ata_status.h"

#include <stdint.h>
#include <stdio.h>

static int g_failures = 0;

static void fail(const char *msg) {
  printf("[ata-status] FAIL: %s\n", msg);
  g_failures++;
}

/* === fatal predicate === */

static void test_fatal_clear_when_zero(void) {
  if (ata_status_is_fatal(0u)) fail("status=0 must not be fatal");
}

static void test_fatal_clear_when_ready_idle(void) {
  /* BSY clear, DRDY set, no DF/ERR -> healthy idle device. */
  if (ata_status_is_fatal(ATA_STATUS_DRDY))
    fail("DRDY alone must not be fatal");
}

static void test_fatal_clear_when_drq(void) {
  if (ata_status_is_fatal(ATA_STATUS_DRQ))
    fail("DRQ alone must not be fatal");
}

static void test_fatal_set_on_err(void) {
  if (!ata_status_is_fatal(ATA_STATUS_ERR)) fail("ERR must be fatal");
}

static void test_fatal_set_on_df(void) {
  if (!ata_status_is_fatal(ATA_STATUS_DF)) fail("DF must be fatal");
}

static void test_fatal_set_on_df_and_err(void) {
  if (!ata_status_is_fatal((uint8_t)(ATA_STATUS_DF | ATA_STATUS_ERR)))
    fail("DF|ERR must be fatal");
}

static void test_fatal_independent_of_ready_bits(void) {
  /* A faulted-but-ready device must still report fatal — this is the
   * exact regression the live fix guards against (BSY cleared, RDY
   * set, but DF/ERR latched). */
  if (!ata_status_is_fatal((uint8_t)(ATA_STATUS_DRDY | ATA_STATUS_ERR)))
    fail("DRDY|ERR must be fatal");
  if (!ata_status_is_fatal((uint8_t)(ATA_STATUS_DRDY | ATA_STATUS_DF)))
    fail("DRDY|DF must be fatal");
}

static void test_fatal_ignores_unrelated_bits(void) {
  /* CORR/IDX/DSC are informational, never failures. */
  uint8_t benign =
      (uint8_t)(ATA_STATUS_CORR | ATA_STATUS_IDX | ATA_STATUS_DSC);
  if (ata_status_is_fatal(benign)) fail("CORR|IDX|DSC must not be fatal");
}

/* === busy / drq predicates === */

static void test_busy_predicate(void) {
  if (!ata_status_busy(ATA_STATUS_BSY)) fail("BSY must report busy");
  if (ata_status_busy(0u)) fail("status=0 must not report busy");
  if (ata_status_busy(ATA_STATUS_DRQ))
    fail("DRQ alone must not report busy");
}

static void test_drq_predicate(void) {
  if (!ata_status_drq_ready(ATA_STATUS_DRQ)) fail("DRQ must report ready");
  if (ata_status_drq_ready(0u)) fail("status=0 must not report DRQ");
  if (ata_status_drq_ready(ATA_STATUS_BSY))
    fail("BSY alone must not report DRQ");
}

static void test_full_status_byte(void) {
  /* 0xFF (floating bus / no device) has both BSY and the fatal bits
   * set; predicates must agree. The live driver treats 0xFF as a
   * distinct no-device sentinel before the BSY/fatal checks. */
  if (!ata_status_busy(0xFFu)) fail("0xFF must report busy");
  if (!ata_status_is_fatal(0xFFu)) fail("0xFF must report fatal");
}

int run_ata_status_tests(void) {
  g_failures = 0;
  test_fatal_clear_when_zero();
  test_fatal_clear_when_ready_idle();
  test_fatal_clear_when_drq();
  test_fatal_set_on_err();
  test_fatal_set_on_df();
  test_fatal_set_on_df_and_err();
  test_fatal_independent_of_ready_bits();
  test_fatal_ignores_unrelated_bits();
  test_busy_predicate();
  test_drq_predicate();
  test_full_status_byte();
  if (g_failures == 0) printf("[tests] ata_status OK\n");
  return g_failures;
}
